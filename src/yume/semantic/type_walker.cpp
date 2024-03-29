#include "type_walker.hpp"
#include "ast/ast.hpp"
#include "compiler/compiler.hpp"
#include "compiler/type_holder.hpp"
#include "compiler/vals.hpp"
#include "diagnostic/errors.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringMapEntry.h>
#include <llvm/ADT/iterator.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Use.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace yume::semantic {
inline void wrap_in_implicit_cast(ast::OptionalExpr& expr, ty::Conv conv, optional<ty::Type> target_type) {
  auto cast_expr = std::make_unique<ast::ImplicitCastExpr>(expr->token_range(), move(expr), conv);
  cast_expr->val_ty(target_type);
  expr = move(cast_expr);
}

inline auto try_implicit_conversion(ast::OptionalExpr& expr, optional<ty::Type> target_ty) -> bool {
  if (!target_ty)
    return false;
  if (!expr)
    return false;

  auto expr_ty = expr->ensure_ty();
  auto compat = expr_ty.compatibility(*target_ty);
  if (!compat.valid)
    return false;

  if (!compat.conv.empty())
    wrap_in_implicit_cast(expr, compat.conv, target_ty);

  return true;
}

void make_implicit_conversion(ast::OptionalExpr& expr, optional<ty::Type> target_ty) {
  if (!target_ty)
    return;
  if (!expr)
    return;

  if (!try_implicit_conversion(expr, target_ty)) {
    throw std::runtime_error("Invalid implicit conversion ('"s + expr->ensure_ty().name() + "' -> '" +
                             target_ty->name() + "', " + std::to_string(expr->ensure_ty().kind()) + " -> " +
                             std::to_string(target_ty->kind()) + ")");
  }
}

template <> void TypeWalker::expression(ast::NumberExpr& expr) {
  auto val = expr.val;
  if (val > std::numeric_limits<int32_t>::max())
    expr.val_ty(compiler.m_types.int64().s_ty);
  else
    expr.val_ty(compiler.m_types.int32().s_ty);
}

template <> void TypeWalker::expression(ast::StringExpr& expr) {
  // TODO(rymiel): #18 String type
  expr.val_ty(create_slice_type(ty::Type(compiler.m_types.int8().u_ty)));
}

template <> void TypeWalker::expression(ast::CharExpr& expr) { expr.val_ty(compiler.m_types.int8().u_ty); }

template <> void TypeWalker::expression(ast::BoolExpr& expr) { expr.val_ty(compiler.m_types.bool_type); }

template <> void TypeWalker::expression(ast::Type& expr) {
  expr.val_ty(convert_type(expr));
  if (auto* qual_type = dyn_cast<ast::QualType>(&expr))
    expression(*qual_type->base);
}

template <> void TypeWalker::expression(ast::TypeName& expr) {
  auto& type = *expr.type;
  expr.attach_to(&type);
  expression(type);
}

template <> void TypeWalker::expression(ast::ImplicitCastExpr& expr) { body_expression(*expr.base); }

static inline auto for_all_instantiations(std::deque<Struct>& structs, std::invocable<Struct&> auto fn) {
  for (auto& i : structs) {
    fn(i);
    for (auto& [k, v] : i.instantiations)
      fn(*v);
  }
}

template <> void TypeWalker::expression(ast::CtorExpr& expr) {
  Struct* st = nullptr;

  expression(*expr.type);
  auto base_type = convert_type(*expr.type);

  for_all_instantiations(compiler.m_structs, [&](Struct& i) {
    if (i.self_ty == base_type)
      st = &i;
  });
  expr.val_ty(base_type);

  const bool consider_ctor_overloads = st != nullptr;
  OverloadSet ctor_overloads{};

  // XXX: Duplicated from function overload handling
  if (consider_ctor_overloads) {
    resolve_queue();
    ctor_overloads = all_ctor_overloads_by_type(*st, expr);
  }

  for (auto& i : expr.args) {
    body_expression(*i);
    ctor_overloads.args.push_back(i.raw_ptr());
  }

  if (consider_ctor_overloads) {
#ifdef YUME_SPEW_OVERLOAD_SELECTION
    errs() << "\n*** BEGIN CTOR OVERLOAD EVALUATION ***\n";
    errs() << "Constructors with matching types:\n";
    ctor_overloads.dump(errs());
#endif

    ctor_overloads.determine_valid_overloads();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
    errs() << "\nViable overloads:\n";
    ctor_overloads.dump(errs(), true);
#endif

    auto best_overload = ctor_overloads.best_viable_overload();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
    errs() << "\nSelected overload:\n";
    best_overload.dump(errs());
    errs() << "\n*** END CTOR OVERLOAD EVALUATION ***\n\n";
#endif

    auto& subs = best_overload.subs;
    auto* selected = best_overload.fn;
    YUME_ASSERT(subs.fully_substituted(), "Constructors cannot be generic"); // TODO(rymiel): revisit?

    // XXX: STILL Duplicated from function overload handling
    // It is an instantiation of a function template
    if (!subs.empty()) {
      // Try to find an already existing instantiation with the same substitutions
      auto [already_existed, inst_fn] = selected->get_or_create_instantiation(subs);
      if (!already_existed) {
        // An existing one wasn't found. We've been given a duplicate of the template's AST but without types
        // The duplicate will have its types set again according to the substitutions being used.
        auto& new_fn = inst_fn;

        // The types of the instantiated function must be set immediately (i.e. with in_depth = false)
        // This is because the implicit cast logic below depends on the direct type being set here and bound type
        // information doesn't propagate across ImplicitCastExpr...
        // TODO(rymiel): Find a better solution; such as moving cast logic also into queue?
        // However, that would require evaluation of the queue very eagerly (i.e. immediately when the function is used,
        // which kinda defeats the purpose of the queue). So I guess we'll just keep this until I think of a better
        // solution
        // TODO(rymiel): find a better solution other than the solution proposed above
        with_saved_scope([&] {
          in_depth = false;
          current_decl = &new_fn;
          body_statement(new_fn.ast());
        });

        decl_queue.emplace(&new_fn);

        selected = &new_fn;
      } else {
        selected = &inst_fn;
      }
    }

    // XXX: STILL Duplicated from function overload handling
    for (auto [target, expr_arg, compat] : llvm::zip(selected->arg_types(), expr.args, best_overload.compatibilities)) {
      YUME_ASSERT(compat.valid, "Invalid compatibility after overload already selected?????");
      if (compat.conv.empty())
        continue;

      wrap_in_implicit_cast(expr_arg, compat.conv, target);
    }

    expr.selected_overload = selected;
  }
}

auto TypeWalker::make_dup(ast::AnyExpr& expr) -> Fn* {
  Struct* st = nullptr;

  auto base_type = expr->ensure_ty().without_mut();

  for_all_instantiations(compiler.m_structs, [&](Struct& i) {
    if (i.self_ty == base_type)
      st = &i;
  });

  YUME_ASSERT(st != nullptr, "Cannot duplicate non-struct type " + base_type.name());
  OverloadSet ctor_overloads{};

  auto ctor_receiver = std::make_unique<ast::SelfType>(expr->token_range());
  ctor_receiver->val_ty(base_type);
  auto ctor_args = vector<ast::AnyExpr>{};
  ctor_args.emplace_back(move(expr));
  auto ctor_expr = std::make_unique<ast::CtorExpr>(ctor_receiver->token_range(), move(ctor_receiver), move(ctor_args));
  ctor_expr->val_ty(base_type);

  // XXX: Duplicated from function overload handling
  resolve_queue();
  ctor_overloads = all_ctor_overloads_by_type(*st, *ctor_expr);
  ctor_overloads.args.push_back(ctor_expr->args.front().raw_ptr());

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\n*** BEGIN CTOR OVERLOAD EVALUATION ***\n";
  errs() << "Constructors with matching types:\n";
  ctor_overloads.dump(errs());
#endif

  ctor_overloads.determine_valid_overloads();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\nViable overloads:\n";
  ctor_overloads.dump(errs(), true);
#endif

  auto best_overload = ctor_overloads.best_viable_overload();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\nSelected overload:\n";
  best_overload.dump(errs());
  errs() << "\n*** END CTOR OVERLOAD EVALUATION ***\n\n";
#endif

  auto& subs = best_overload.subs;
  auto* selected = best_overload.fn;

  YUME_ASSERT(subs.fully_substituted(), "Constructors cannot be generic"); // TODO(rymiel): revisit?

  // XXX: STILL Duplicated from function overload handling
  // It is an instantiation of a function template
  if (!subs.empty()) {
    // Try to find an already existing instantiation with the same substitutions
    auto [already_existed, inst_fn] = selected->get_or_create_instantiation(subs);
    if (!already_existed) {
      // An existing one wasn't found. We've been given a duplicate of the template's AST but without types
      // The duplicate will have its types set again according to the substitutions being used.
      auto& new_fn = inst_fn;

      // The types of the instantiated function must be set immediately (i.e. with in_depth = false)
      // This is because the implicit cast logic below depends on the direct type being set here and bound type
      // information doesn't propagate across ImplicitCastExpr...
      // TODO(rymiel): Find a better solution; such as moving cast logic also into queue?
      // However, that would require evaluation of the queue very eagerly (i.e. immediately when the function is used,
      // which kinda defeats the purpose of the queue). So I guess we'll just keep this until I think of a better
      // solution
      // TODO(rymiel): find a better solution other than the solution proposed above
      with_saved_scope([&] {
        in_depth = false;
        current_decl = &new_fn;
        body_statement(new_fn.ast());
      });

      decl_queue.emplace(&new_fn);

      selected = &new_fn;
    } else {
      selected = &inst_fn;
    }
  }

  // XXX: STILL Duplicated from function overload handling
  auto target = selected->arg_types().front();
  auto& expr_arg = ctor_expr->args.front();
  auto compat = best_overload.compatibilities.front();
  YUME_ASSERT(compat.valid, "Invalid compatibility after overload already selected?????");
  if (!compat.conv.empty())
    wrap_in_implicit_cast(expr_arg, compat.conv, target);
  expr = move(ctor_expr);

  return selected;
}

template <> void TypeWalker::expression(ast::SliceExpr& expr) {
  for (auto& i : expr.args)
    body_expression(*i);

  auto& slice_base = *expr.type;
  expression(slice_base);
  auto base_type = create_slice_type(convert_type(slice_base));

  // expr.val_ty(&base_type.known_slice());
  expr.val_ty(base_type);
}

template <> void TypeWalker::expression(ast::LambdaExpr& expr) {
  enclosing_scopes.push_back(scope);
  with_saved_scope([&] {
    scope.clear();
    [[maybe_unused]] auto guard = scope.push_scope_guarded();

    auto arg_types = vector<ty::Type>{};
    auto ret_type = optional<ty::Type>{};

    for (auto& i : expr.args) {
      expression(i);
      scope.add(i.name, &i);
      arg_types.push_back(i.ensure_ty());
    }

    if (expr.ret.has_value()) {
      expression(*expr.ret);
      ret_type = expr.ret->ensure_ty();
    }

    body_statement(expr.body);

    auto closured_types = vector<ty::Type>();
    for (const auto& i : closured) {
      closured_types.push_back(i.ast->ensure_ty());
      expr.closured_names.push_back(i.name);
      expr.closured_nodes.push_back(i.ast);
    }

    expr.val_ty(compiler.m_types.find_or_create_fn_type(arg_types, ret_type, closured_types));
  });
  enclosing_scopes.pop_back();
}

void TypeWalker::direct_call_operator(ast::CallExpr& expr) {
  YUME_ASSERT(expr.args.size() > 1, "Direct call must have at least 1 argument");
  auto& base = *expr.args.front();
  body_expression(base);

  YUME_ASSERT(base.ensure_ty().base_isa<ty::Function>(), "Direct call target must be a function type");
  const auto* base_ptr_ty = base.ensure_ty().base_cast<ty::Function>();

  for (auto [target, expr_arg] : llvm::zip(base_ptr_ty->args(), llvm::drop_begin(expr.args))) {
    body_expression(*expr_arg);

    auto compat = expr_arg->ensure_ty().compatibility(target);
    YUME_ASSERT(compat.valid, "Invalid direct call with incompatible argument types");
    if (compat.conv.empty())
      continue;

    wrap_in_implicit_cast(expr_arg, compat.conv, target);
  }

  expr.val_ty(base_ptr_ty->ret());
}

template <> void TypeWalker::expression(ast::AssignExpr& expr) {
  body_expression(*expr.target);
  body_expression(*expr.value);

  make_implicit_conversion(expr.value, expr.target->ensure_ty().mut_base());

  expr.target->attach_to(expr.value.raw_ptr());
  expr.attach_to(expr.value.raw_ptr());
}

template <> void TypeWalker::expression(ast::VarExpr& expr) {
  if (auto** var = scope.find(expr.name); var != nullptr)
    return expr.attach_to(*var);

  // If we're inside a lambda body, check if the variable maybe refers to one from an outer scope that can be
  // included in the closure of the current lambda
  for (auto& outer_scope : enclosing_scopes) {
    if (auto** var = outer_scope.find(expr.name); var != nullptr) {
      // It was found, include it in the current scope, so we don't need to look for it again
      scope.add_to_front(expr.name, *var);
      closured.push_back({*var, expr.name});
      return expr.attach_to(*var);
    }
  }

  throw std::runtime_error("Scope doesn't contain variable called "s + expr.name);
}

template <> void TypeWalker::expression(ast::ConstExpr& expr) {
  for (const auto& cn : compiler.m_consts)
    if (cn.referred_to_by(expr))
      return expr.val_ty(cn.ast().ensure_ty());
  throw std::runtime_error("Nonexistent constant called "s + expr.name);
}

static auto find_field_ast(const ty::Struct& st, string_view target_name) -> std::pair<nullable<ast::AnyType*>, int> {
  nullable<ast::AnyType*> target_type = nullptr;
  int j = 0;
  for (auto* field : st.fields()) {
    if (field->name == target_name) {
      target_type = &field->type;
      break;
    }
    j++;
  }

  if (target_type == nullptr)
    j = -1;

  return {target_type, j};
}

static auto find_field(const ty::Struct& st, string_view target_name) -> std::pair<optional<ty::Type>, int> {
  optional<ty::Type> target_type;
  auto [ast_ptr, j] = find_field_ast(st, target_name);
  if (ast_ptr != nullptr)
    target_type = ast_ptr->raw_ptr()->val_ty();

  return {target_type, j};
}

template <> void TypeWalker::expression(ast::FieldAccessExpr& expr) {
  optional<ty::Type> type;
  bool base_is_mut = false;

  if (expr.base.has_value()) {
    body_expression(*expr.base);
    type = expr.base->ensure_ty();

    if (type->is_mut()) {
      type = type->mut_base();
      base_is_mut = true;
    };
  } else {
    type = current_decl.self_ty();
    base_is_mut = true;
  }

  if (!type.has_value())
    llvm_unreachable("Type must be set in either branch above");

  if (type->is_opaque_self())
    make_implicit_conversion(expr.base, type->without_opaque());

  const auto* struct_type = type->without_opaque().base_dyn_cast<ty::Struct>();

  if (struct_type == nullptr)
    throw std::runtime_error("Can't access field of expression with non-struct type");

  auto target_name = expr.field;
  auto [target_type, target_offset] = find_field(*struct_type, target_name);

  expr.offset = target_offset;
  expr.val_ty(base_is_mut ? target_type->known_mut() : target_type);
}

auto TypeWalker::all_fn_overloads_by_name(ast::CallExpr& call) -> OverloadSet {
  auto fns_by_name = vector<Overload>();

  for (auto& fn : compiler.m_fns)
    if (fn.name() == call.name)
      fns_by_name.emplace_back(&fn);

  return OverloadSet{&call, fns_by_name, {}};
}

auto TypeWalker::all_ctor_overloads_by_type(Struct& st, ast::CtorExpr& call) -> OverloadSet {
  auto ctors_by_type = vector<Overload>();

  for (auto& ctor : compiler.m_ctors)
    if (ctor.self_ty && st.self_ty && *ctor.self_ty == st.self_ty->generic_base())
      ctors_by_type.emplace_back(&ctor);

  return OverloadSet{&call, ctors_by_type, {}};
}

template <> void TypeWalker::expression(ast::CallExpr& expr) {
  auto name = expr.name;

  if (name == "->") // TODO(rymiel): Magic value?
    return direct_call_operator(expr);

  auto overload_set = all_fn_overloads_by_name(expr);

  if (overload_set.empty()) {                      // HACK
    resolve_queue();                               // HACK
    overload_set = all_fn_overloads_by_name(expr); // HACK
  }                                                // HACK

  if (overload_set.empty())
    throw std::logic_error("No function overload named "s + name);

  if (expr.receiver.has_value())
    expression(*expr.receiver);

  for (auto& i : expr.args) {
    body_expression(*i);
    overload_set.args.push_back(i.raw_ptr());
  }

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\n*** BEGIN FN OVERLOAD EVALUATION ***\n";
  errs() << "Functions with matching names:\n";
  overload_set.dump(errs());
#endif

  overload_set.determine_valid_overloads();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\nViable overloads:\n";
  overload_set.dump(errs(), true);
#endif

  const auto* maybe_best_overload = overload_set.try_best_viable_overload(); // HACK
  if (maybe_best_overload == nullptr && !decl_queue.empty()) {               // HACK
    resolve_queue();                                                         // HACK
    return expression(expr);                                                 // HACK
  }                                                                          // HACK
  Overload best_overload = overload_set.best_viable_overload();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\nSelected overload:\n";
  best_overload.dump(errs());
  errs() << "\n*** END FN OVERLOAD EVALUATION ***\n\n";
#endif

  auto& subs = best_overload.subs;
  auto* selected = best_overload.fn;

  // It is an instantiation of a function template
  if (!subs.empty()) {
    // Try to find an already existing instantiation with the same substitutions
    auto [already_existed, inst_fn] = selected->get_or_create_instantiation(subs);
    if (!already_existed) {
      // An existing one wasn't found. We've been given a duplicate of the template's AST but without types
      // The duplicate will have its types set again according to the substitutions being used.
      auto& new_fn = inst_fn;

      // The types of the instantiated function must be set immediately (i.e. with in_depth = false)
      // This is because the implicit cast logic below depends on the direct type being set here and bound type
      // information doesn't propagate across ImplicitCastExpr...
      // TODO(rymiel): Find a better solution; such as moving cast logic also into queue?
      // However, that would require evaluation of the queue very eagerly (i.e. immediately when the function is used,
      // which kinda defeats the purpose of the queue). So I guess we'll just keep this until I think of a better
      // solution
      // TODO(rymiel): find a better solution other than the solution proposed above
      with_saved_scope([&] {
        in_depth = false;
        current_decl = &new_fn;
        body_statement(new_fn.ast());
      });

      decl_queue.emplace(&new_fn);

      selected = &new_fn;
    } else {
      selected = &inst_fn;
    }
  }

  for (auto [target, expr_arg, compat] : llvm::zip(selected->arg_types(), expr.args, best_overload.compatibilities)) {
    YUME_ASSERT(compat.valid, "Invalid compatibility after overload already selected?????");
    if (compat.conv.empty())
      continue;

    wrap_in_implicit_cast(expr_arg, compat.conv, target);
  }

  // Find excess variadic arguments. Logic will probably change later, but for now, always pass by value
  // TODO(rymiel): revisit
  for (const auto& expr_arg : llvm::enumerate(expr.args)) {
    if (expr_arg.index() >= selected->arg_count() && expr_arg.value()->ensure_ty().is_mut()) {
      auto target_type = expr_arg.value()->ensure_ty().mut_base();
      wrap_in_implicit_cast(expr_arg.value(), ty::Conv{.dereference = true}, target_type);
    }
  }

  if (selected->ret().has_value())
    expr.attach_to(&selected->ast());

  expr.selected_overload = selected;
}

template <> void TypeWalker::expression(ast::BinaryLogicExpr& expr) {
  // TODO(rymiel): expand this to apply more than just bools
  ty::Type bool_ty = compiler.m_types.bool_type;
  body_expression(*expr.lhs);
  body_expression(*expr.rhs);
  YUME_ASSERT(expr.lhs->ensure_ty() == bool_ty, "BinaryLogicExpr lhs must be boolean");
  YUME_ASSERT(expr.rhs->ensure_ty() == bool_ty, "BinaryLogicExpr rhs must be boolean");
  expr.val_ty(bool_ty);
}

template <> void TypeWalker::expression(ast::TypeExpr& expr) {
  expression(*expr.type);
  ty::Type base_type = expr.type->ensure_ty();
  YUME_ASSERT(base_type.is_unqualified(), "Type expression must be unqualified"); // TODO(rymiel): revisit
  expr.val_ty(base_type.known_meta());
}

template <> void TypeWalker::statement(ast::Compound& stat) {
  [[maybe_unused]] auto guard = scope.push_scope_guarded();
  for (auto& i : stat)
    body_statement(*i);
}

template <> void TypeWalker::statement(ast::StructDecl& stat) {
  for (auto& type_arg : stat.type_args)
    if (type_arg.type.has_value())
      expression(*type_arg.type);

  // This decl still has unsubstituted generics, can't instantiate its body
  if (!current_decl.fully_substituted())
    return;

  for (auto& i : stat.fields)
    expression(i);

  if (stat.implements)
    expression(*stat.implements);
}

template <> void TypeWalker::statement(ast::FnDecl& stat) {
  scope.clear();
  [[maybe_unused]] auto guard = scope.push_scope_guarded();

  auto args = vector<ty::Type>();
  auto ret = optional<ty::Type>();

  for (auto& i : stat.args) {
    expression(i);
    scope.add(i.name, &i);
    args.push_back(i.ensure_ty());
  }

  if (stat.ret.has_value()) {
    expression(*stat.ret);
    stat.attach_to(stat.ret.raw_ptr());
    ret = stat.ret->ensure_ty();
  }

  std::get<Fn*>(current_decl)->fn_ty = compiler.m_types.find_or_create_fn_ptr_type(args, ret, stat.varargs());

  // This decl still has unsubstituted generics, can't instantiate its body
  if (!current_decl.fully_substituted())
    return;

  if (in_depth && std::holds_alternative<ast::Compound>(stat.body))
    statement(get<ast::Compound>(stat.body));

  YUME_ASSERT(scope.size() == 1, "End of function should end with only the function scope remaining");
}

template <> void TypeWalker::statement(ast::CtorDecl& stat) {
  scope.clear();
  [[maybe_unused]] auto guard = scope.push_scope_guarded();

  const auto* struct_type = current_decl.self_ty()->without_mut().base_dyn_cast<ty::Struct>();
  auto args = vector<ty::Type>();

  if (struct_type == nullptr)
    throw std::runtime_error("Can't define constructor of non-struct type");

  stat.val_ty(struct_type);
  for (auto& i : stat.args) {
    expression(i);
    scope.add(i.name, &i);
    args.push_back(i.ensure_ty());
  }

  for (auto& i : stat.args) {
    expression(i);
    scope.add(i.name, &i);
  }

  std::get<Fn*>(current_decl)->fn_ty = compiler.m_types.find_or_create_fn_ptr_type(args, current_decl.self_ty());

  // This decl still has unsubstituted generics, can't instantiate its body
  if (!current_decl.fully_substituted())
    return;

  if (in_depth)
    statement(stat.body);

  YUME_ASSERT(scope.size() == 1, "End of function should end with only the function scope remaining");
}

template <> void TypeWalker::statement(ast::ReturnStmt& stat) {
  if (stat.expr.has_value()) {
    body_expression(*stat.expr);
    // If we're returning a local variable, mark that it will leave the scope and should not be destructed yet.
    if (auto* var_expr = dyn_cast<ast::VarExpr>(stat.expr.raw_ptr())) {
      if (auto** in_scope = scope.find(var_expr->name); in_scope != nullptr) {
        if (auto* var_decl = dyn_cast<ast::VarDecl>(*in_scope))
          stat.extends_lifetime = var_decl;
      }
    }

    make_implicit_conversion(stat.expr, current_decl.ast()->val_ty());
    current_decl.ast()->attach_to(stat.expr.raw_ptr());
    // TODO(rymiel): Once return type deduction exists, make sure to not return `mut` unless there is an _explicit_ type
    // annotation saying so
  }
}

template <> void TypeWalker::statement(ast::VarDecl& stat) {
  body_expression(*stat.init);
  if (stat.type.has_value()) {
    expression(*stat.type);
    make_implicit_conversion(stat.init, stat.type->val_ty());
    stat.init->attach_to(stat.type.raw_ptr());
  } else {
    // This does a "decay" of sorts. If an explicit type isn't provided, and the initializer returns a mutable
    // reference, the variable is initialized from a value instead of a reference. Note that the local variable itself
    // becomes a reference again, but usually as a copy of the initializer.
    // TODO(rymiel): Add a way to bypass this decay, i.e. by using `let mut`.
    make_implicit_conversion(stat.init, stat.init->ensure_ty().without_mut());
  }

  stat.val_ty(stat.init->ensure_ty().known_mut());
  scope.add(stat.name, &stat);
}

template <> void TypeWalker::statement(ast::ConstDecl& stat) {
  expression(*stat.type);
  if (in_depth) {
    body_expression(*stat.init);
    // TODO(rymiel): Perform literal casts
    make_implicit_conversion(stat.init, stat.type->val_ty());
    stat.init->attach_to(stat.type.raw_ptr());
  }
  stat.val_ty(stat.type->ensure_ty());
}

template <> void TypeWalker::statement(ast::IfStmt& stat) {
  for (auto& i : stat.clauses) {
    body_expression(*i.cond);
    statement(i.body);
  }
  auto& else_clause = stat.else_clause;
  if (else_clause)
    statement(*else_clause);
}

template <> void TypeWalker::statement(ast::WhileStmt& stat) {
  body_expression(*stat.cond);
  statement(stat.body);
}

void TypeWalker::body_statement(ast::Stmt& stat) {
  const ASTStackTrace guard("Semantic: "s + stat.kind_name() + " statement", stat);
  return CRTPWalker::body_statement(stat);
}
void TypeWalker::body_expression(ast::Expr& expr) {
  const ASTStackTrace guard("Semantic: "s + expr.kind_name() + " expression", expr);
  return CRTPWalker::body_expression(expr);
}

auto TypeWalker::get_or_declare_instantiation(Struct* struct_obj, Substitutions subs) -> ty::Type {
  auto [already_existed, inst_struct] = struct_obj->get_or_create_instantiation(subs);

  if (!already_existed) {
    auto& new_st = inst_struct;

    with_saved_scope([&] {
      in_depth = false;
      current_decl = &new_st;
      body_statement(new_st.st_ast);
    });

    if (compiler.create_struct(new_st))
      decl_queue.emplace(&new_st);
  }

  return *inst_struct.self_ty;
}

auto TypeWalker::create_slice_type(const ty::Type& base_type) -> ty::Type {
  YUME_ASSERT(compiler.m_slice_struct != nullptr, "Can't create slice type if a slice type was never defined");
  Struct* struct_obj = compiler.m_slice_struct;
  Substitutions subs = struct_obj->subs;
  subs.associate(*subs.all_keys().at(0), {base_type});
  return get_or_declare_instantiation(struct_obj, subs);
}

auto TypeWalker::convert_type(ast::Type& ast_type) -> ty::Type {
  auto parent = current_decl.self_ty();
  const auto* context = current_decl.subs();

  if (const auto* simple_type = dyn_cast<ast::SimpleType>(&ast_type)) {
    auto name = simple_type->name;
    if (context != nullptr && !context->empty()) {
      const auto* generic = context->mapping_ref_or_null({name});
      if (generic != nullptr && generic->holds_type())
        return generic->as_type();
      if (generic != nullptr && generic->unassigned())
        return context->get_generic_fallback(name);
    }
    auto val = compiler.m_types.known.find(name);
    if (val != compiler.m_types.known.end())
      return val->second.get();
  } else if (auto* qual_type = dyn_cast<ast::QualType>(&ast_type)) {
    auto qualifier = qual_type->qualifier;
    return convert_type(*qual_type->base).known_qual(qualifier);
  } else if (isa<ast::SelfType>(ast_type)) {
    if (parent) {
      if (current_decl.opaque_self())
        return {parent->known_opaque().base(), parent->is_mut(), parent->is_ref()};
      return {parent->base(), parent->is_mut(), parent->is_ref()}; // TODO(rymiel): Isn't this just `parent`
    }
  } else if (auto* proxy_type = dyn_cast<ast::ProxyType>(&ast_type)) {
    if (parent) {
      if (const auto* parent_struct = parent->base_dyn_cast<ty::Struct>()) {
        auto [target_type, target_offset] = find_field_ast(*parent_struct, proxy_type->field);
        if (target_type != nullptr)
          return convert_type(*target_type->raw_ptr());
        throw std::runtime_error("Proxy type doesn't refer to a valid field?");
      }
    }
  } else if (auto* fn_type = dyn_cast<ast::FunctionType>(&ast_type)) {
    auto ret = optional<ty::Type>{};
    auto args = vector<ty::Type>{};
    if (auto& ast_ret = fn_type->ret; ast_ret.has_value())
      ret = convert_type(*ast_ret);

    for (auto& ast_arg : fn_type->args)
      args.push_back(convert_type(*ast_arg));

    if (fn_type->fn_ptr)
      return compiler.m_types.find_or_create_fn_ptr_type(args, ret);
    return compiler.m_types.find_or_create_fn_type(args, ret, {});
  } else if (auto* templated = dyn_cast<ast::TemplatedType>(&ast_type)) {
    auto& template_base = *templated->base;
    expression(template_base);
    auto base_type = convert_type(template_base);
    auto* struct_obj = base_type.base_cast<ty::Struct>()->decl();

    if (struct_obj == nullptr)
      throw std::logic_error("Can't add template arguments to non-struct types");

    for (auto& i : templated->type_args)
      i.visit([&](ast::AnyType& v) { expression(*v); }, [&](ast::AnyExpr& v) { body_expression(*v); });

    Substitutions gen_base = struct_obj->get_subs();
    size_t i = 0;
    for (const auto& [gen, gen_sub] : llvm::zip(gen_base.all_keys(), templated->type_args)) {
      if (gen->holds_type()) {
        gen_base.associate(*gen, {gen_sub.as_type()->ensure_ty()});
      } else {
        auto* expr = gen_sub.as_expr().raw_ptr();
        auto expected_type = struct_obj->st_ast.type_args.at(i).type->ensure_ty();
        auto compat = expr->ensure_ty().compatibility(expected_type);
        YUME_ASSERT(compat.valid, "Non-type generic argument type must match or be implicitly convertible (got `" +
                                      expr->ensure_ty().name() + "', expected `" + expected_type.name() + "')");

        gen_base.associate(*gen, {expr});
      }
      ++i;
    }

    // YUME_ASSERT(gen_base.fully_substituted(), "Can't convert templated type which isn't fully substituted: "s +
    //                                               template_base.describe() + " (" + ast_type.describe() + ")");

    return get_or_declare_instantiation(struct_obj, gen_base);
  }

  throw std::runtime_error("Cannot convert AST type to actual type! "s + ast_type.kind_name() + " (" +
                           ast_type.describe() + ")");
}

void TypeWalker::resolve_queue() {
  while (!decl_queue.empty()) {
    auto next = decl_queue.front();
    decl_queue.pop();

    with_saved_scope([&] {
      next.visit([](std::monostate /* absent */) {}, //
                 [](Const* /* cn */) {},
                 [&](Fn* fn) {
                   in_depth = true;
                   compiler.declare(*fn);
                 },
                 [](Struct* /* st */) {});
    });
  }
}
} // namespace yume::semantic
