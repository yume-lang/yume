#include "type_walker.hpp"
#include "ast/ast.hpp"
#include "compiler/compiler.hpp"
#include "compiler/type_holder.hpp"
#include "compiler/vals.hpp"
#include "diagnostic/errors.hpp"
#include "stl_util.hpp"
#include "ty/compatibility.hpp"
#include "ty/type.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringMapEntry.h>
#include <llvm/ADT/iterator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace yume::semantic {

template <> void TypeWalker::expression(ast::NumberExpr& expr) {
  auto val = expr.val();
  if (val > std::numeric_limits<int32_t>::max()) {
    expr.val_ty(compiler.m_types.int64().s_ty);
  } else {
    expr.val_ty(compiler.m_types.int32().s_ty);
  }
}

template <> void TypeWalker::expression(ast::StringExpr& expr) {
  // TODO: String type
  expr.val_ty(&compiler.m_types.int8().u_ty->known_ptr());
}

template <> void TypeWalker::expression(ast::CharExpr& expr) { expr.val_ty(compiler.m_types.int8().u_ty); }

template <> void TypeWalker::expression(ast::BoolExpr& expr) { expr.val_ty(compiler.m_types.bool_type); }

template <> void TypeWalker::expression(ast::Type& expr) {
  const auto* resolved_type = &convert_type(expr);
  expr.val_ty(resolved_type);
  if (auto* qual_type = dyn_cast<ast::QualType>(&expr))
    expression(qual_type->base());
}

template <> void TypeWalker::expression(ast::TypeName& expr) {
  auto& type = expr.type();
  expr.attach_to(&type);
  expression(type);
}

template <> void TypeWalker::expression(ast::ImplicitCastExpr& expr) { body_expression(expr.base()); }

template <> void TypeWalker::expression(ast::CtorExpr& expr) {
  Struct* st = nullptr;

  if (auto* templated = dyn_cast<ast::TemplatedType>(&expr.type())) {
    auto& template_base = templated->base();
    expression(template_base);
    const auto& base_type = convert_type(template_base);
    auto struct_obj =
        std::ranges::find_if(compiler.m_structs, [&](const Struct& st) { return st.self_ty == &base_type; });

    if (struct_obj == compiler.m_structs.end())
      throw std::logic_error("Can't add template arguments to non-struct types");

    for (auto& i : templated->type_vars())
      expression(*i);

    // XXX: Duplicated from function overload handling
    Instantiation instantiation = {};
    for (const auto& [gen, gen_sub] : llvm::zip(struct_obj->type_args, templated->type_vars()))
      instantiation.sub.try_emplace(gen.get(), gen_sub->val_ty());

    auto [already_existed, inst_struct] = struct_obj->get_or_create_instantiation(instantiation);

    if (!already_existed) {
      auto& new_st = inst_struct;

      with_saved_scope([&] {
        in_depth = false;
        current_decl = &new_st;
        body_statement(new_st.st_ast);
      });

      // HACK: the "describe" method is being abused here
      std::string name_with_types = new_st.st_ast.name() + templated->describe();

      auto& i_ty =
          compiler.m_types.template_instantiations.emplace_back(Compiler::create_struct(new_st.st_ast, name_with_types));

      new_st.self_ty = i_ty.get();
      decl_queue.push(&new_st);
    }

    expr.val_ty(inst_struct.self_ty);
    st = &inst_struct;
  } else {
    expression(expr.type());
    const auto& base_type = convert_type(expr.type());
    auto struct_obj =
        std::ranges::find_if(compiler.m_structs, [&](const Struct& st) { return st.self_ty == &base_type; });
    if (struct_obj != compiler.m_structs.end())
      st = &*struct_obj;
    expr.val_ty(&base_type);
  }

  bool consider_ctor_overloads = st != nullptr;
  OverloadSet<Ctor> ctor_overloads{};

  // XXX: Duplicated from function overload handling
  if (consider_ctor_overloads)
    ctor_overloads = all_ctor_overloads_by_type(*st, expr);

  for (auto& i : expr.args()) {
    body_expression(i);
    ctor_overloads.args.push_back(&i);
  }

  if (consider_ctor_overloads) {
#ifdef YUME_SPEW_OVERLOAD_SELECTION
    llvm::errs() << "\n*** BEGIN CTOR OVERLOAD EVALUATION ***\n";
    llvm::errs() << "Constructors with matching names:\n";
    ctor_overloads.dump(llvm::errs());
#endif

    ctor_overloads.determine_valid_overloads();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
    llvm::errs() << "\nViable overloads:\n";
    ctor_overloads.dump(llvm::errs(), true);
#endif

    const auto* best_overload = ctor_overloads.try_best_viable_overload();
    if (best_overload == nullptr)
      return; // TODO(rymiel): The implicitly defined ctors should be real ctors, so this could be an error

#ifdef YUME_SPEW_OVERLOAD_SELECTION
    llvm::errs() << "\nSelected overload:\n";
    best_overload->dump(llvm::errs());
    llvm::errs() << "\n*** END CTOR OVERLOAD EVALUATION ***\n\n";
#endif

    const auto& instantiate = best_overload->instantiation;
    auto* selected = best_overload->fn;
    yume_assert(instantiate.sub.empty(), "Constructors cannot be generic"); // TODO(rymiel): revisit?

    expr.selected_overload(selected);
  }
}

template <> void TypeWalker::expression(ast::SliceExpr& expr) {
  for (auto& i : expr.args()) {
    body_expression(i);
  }
  expression(expr.type());
  const auto& base_type = convert_type(expr.type());
  expr.val_ty(&base_type.known_slice());
}

template <> void TypeWalker::expression(ast::AssignExpr& expr) {
  body_expression(expr.target());
  body_expression(expr.value());
  expr.target().attach_to(&expr.value());
}

template <> void TypeWalker::expression(ast::VarExpr& expr) {
  if (!scope.contains(expr.name()))
    throw std::runtime_error("Scope doesn't contain variable called "s + expr.name());
  expr.attach_to(scope.at(expr.name()));
}

template <> void TypeWalker::expression(ast::FieldAccessExpr& expr) {
  if (!expr.base().has_value())
    return; // TODO: Only valid inside constructors

  body_expression(*expr.base());
  const auto& type = *expr.base()->get().val_ty();

  const auto* struct_type = dyn_cast<ty::Struct>(&type.without_qual());

  if (struct_type == nullptr)
    throw std::runtime_error("Can't access field of expression with non-struct type");

  auto target_name = expr.field();
  const ty::Type* target_type{};
  int j = 0;
  for (const auto& field : struct_type->fields()) {
    if (field.name() == target_name) {
      target_type = field.type().val_ty();
      break;
    }
    j++;
  }

  expr.offset(j);
  expr.val_ty(target_type);
}

auto TypeWalker::all_fn_overloads_by_name(ast::CallExpr& call) -> OverloadSet<Fn> {
  auto fns_by_name = vector<Overload<Fn>>();

  for (auto& fn : compiler.m_fns)
    if (fn.name() == call.name())
      fns_by_name.emplace_back(&fn);

  return OverloadSet<Fn>{&call, fns_by_name, {}};
}

auto TypeWalker::all_ctor_overloads_by_type(Struct& st, ast::CtorExpr& call) -> OverloadSet<Ctor> {
  auto ctors_by_type = vector<Overload<Ctor>>();

  for (auto& ctor : compiler.m_ctors)
    if (ctor.base.self_ty == st.self_ty)
      ctors_by_type.emplace_back(&ctor);

  return OverloadSet<Ctor>{&call, ctors_by_type, {}};
}

template <> void TypeWalker::expression(ast::CallExpr& expr) {
  resolve_queue();

  auto overload_set = all_fn_overloads_by_name(expr);
  auto name = expr.name();

  if (overload_set.empty())
    throw std::logic_error("No function overload named "s + name);

  for (auto& i : expr.args()) {
    body_expression(i);
    overload_set.args.push_back(&i);
  }

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  llvm::errs() << "\n*** BEGIN FN OVERLOAD EVALUATION ***\n";
  llvm::errs() << "Functions with matching names:\n";
  overload_set.dump(llvm::errs());
#endif

  overload_set.determine_valid_overloads();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  llvm::errs() << "\nViable overloads:\n";
  overload_set.dump(llvm::errs(), true);
#endif

  Overload best_overload = overload_set.best_viable_overload();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  llvm::errs() << "\nSelected overload:\n";
  best_overload.dump(llvm::errs());
  llvm::errs() << "\n*** END FN OVERLOAD EVALUATION ***\n\n";
#endif

  auto& instantiate = best_overload.instantiation;
  auto* selected = best_overload.fn;

  // It is an instantiation of a function template
  if (!instantiate.sub.empty()) {
    // Try to find an already existing instantiation with the same substitutions
    auto [already_existed, inst_fn] = selected->get_or_create_instantiation(instantiate);
    if (!already_existed) {
      // An existing one wasn't found. We've been given a duplicate of the template's AST but without types
      // The duplicate will have its types set again according to the substitutions being used.
      auto& new_fn = inst_fn;

      // The types of the instantiated function must be set immediately (i.e. with in_depth = false)
      // This is because the implicit cast logic below depends on the direct type being set here and bound type
      // information doesn't propagate across ImplicitCastExpr...
      // TODO: Find a better solution; such as moving cast logic also into queue?
      // However that would require evaluation of the queue very eagerly (i.e. immediately when the function is used,
      // which kinda defeats the purpose of the queue). So I guess we'll just keep this until I think of a better
      // solution
      // TODO: find a better solution other than the solution proposed above
      with_saved_scope([&] {
        in_depth = false;
        current_decl = &new_fn;
        body_statement(new_fn.ast());
      });

      decl_queue.push(&new_fn);

      selected = &new_fn;
    } else {
      selected = &inst_fn;
    }
  }

  for (auto [target, expr_arg, compat] :
       llvm::zip(selected->ast().args(), expr.direct_args(), best_overload.compatibilities)) {
    yume_assert(compat.valid, "Invalid compatibility after overload already selected?????");
    if (compat.conv.empty())
      continue;

    const auto* target_type = target.type().val_ty();
    auto cast_expr = std::make_unique<ast::ImplicitCastExpr>(expr_arg->token_range(), move(expr_arg), compat.conv);
    cast_expr->val_ty(target_type);
    expr_arg = move(cast_expr);
  }

  if (selected->ast().ret().has_value())
    expr.attach_to(&selected->ast());

  expr.selected_overload(selected);
}

template <> void TypeWalker::statement(ast::Compound& stat) {
  for (auto& i : stat.body())
    body_statement(i);
}

template <> void TypeWalker::statement(ast::StructDecl& stat) {
  // This decl still has unsubstituted generics, can't instantiate its body
  if (std::ranges::any_of(*current_decl.subs(), [](const auto& sub) { return sub.second->is_generic(); }))
    return;

  for (auto& i : stat.fields())
    expression(i);
}

template <> void TypeWalker::statement(ast::FnDecl& stat) {
  scope.clear();

  for (auto& i : stat.args()) {
    expression(i);
    scope.insert({i.name(), &i});
  }

  if (stat.ret().has_value()) {
    expression(stat.ret()->get());
    stat.attach_to(&stat.ret()->get());
  }

  // This decl still has unsubstituted generics, can't instantiate its body
  if (std::ranges::any_of(*current_decl.subs(), [](const auto& sub) { return sub.second->is_generic(); }))
    return;

  if (in_depth && std::holds_alternative<ast::Compound>(stat.body()))
    statement(get<ast::Compound>(stat.body()));
}

template <> void TypeWalker::statement(ast::CtorDecl& stat) {
  scope.clear();

  const auto* struct_type = dyn_cast<ty::Struct>(&current_decl.self_ty()->without_qual());

  if (struct_type == nullptr)
    throw std::runtime_error("Can't define constructor of non-struct type");

  for (auto& i : stat.args()) {
    if (auto* type_name = std::get_if<ast::TypeName>(&i)) {
      expression(*type_name);
      scope.insert({type_name->name(), type_name});
    } else if (auto* direct_init = std::get_if<ast::VarExpr>(&i)) {
      auto target_name = direct_init->name();
      const ty::Type* target_type{};
      for (const auto& field : struct_type->fields()) {
        if (field.name() == target_name) {
          target_type = field.type().val_ty();
          break;
        }
      }

      direct_init->val_ty(target_type);
      scope.insert({target_name, direct_init});
    }
  }

  if (in_depth)
    statement(stat.body());
}

template <> void TypeWalker::statement(ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    auto& returned = stat.expr()->get();
    body_expression(returned);
    current_decl.ast()->attach_to(&returned);
  }
}

template <> void TypeWalker::statement(ast::VarDecl& stat) {
  body_expression(stat.init());
  if (stat.type().has_value()) {
    expression(stat.type()->get());
    stat.init().attach_to(&stat.type()->get());
  }

  stat.val_ty(&stat.init().val_ty()->known_mut());
  scope.insert({stat.name(), &stat});
}

template <> void TypeWalker::statement(ast::IfStmt& stat) {
  for (auto& i : stat.clauses()) {
    body_expression(i.cond());
    statement(i.body());
  }
  if (stat.else_clause().has_value())
    statement(*stat.else_clause());
}

template <> void TypeWalker::statement(ast::WhileStmt& stat) {
  body_expression(stat.cond());
  statement(stat.body());
}

void TypeWalker::body_statement(ast::Stmt& stat) {
  const ASTStackTrace guard("Semantic: "s + stat.kind_name() + " statement", stat);
  return CRTPWalker::body_statement(stat);
}
void TypeWalker::body_expression(ast::Expr& expr) {
  const ASTStackTrace guard("Semantic: "s + expr.kind_name() + " expression", expr);
  return CRTPWalker::body_expression(expr);
}

auto TypeWalker::convert_type(const ast::Type& ast_type) -> const ty::Type& {
  const ty::Type* parent = current_decl.self_ty();
  const auto* context = current_decl.subs();

  if (const auto* simple_type = dyn_cast<ast::SimpleType>(&ast_type)) {
    auto name = simple_type->name();
    if (context != nullptr) {
      auto generic = context->find(name);
      if (generic != context->end())
        return *generic->second;
    }
    auto val = compiler.m_types.known.find(name);
    if (val != compiler.m_types.known.end())
      return *val->second;
  } else if (const auto* qual_type = dyn_cast<ast::QualType>(&ast_type)) {
    auto qualifier = qual_type->qualifier();
    return convert_type(qual_type->base()).known_qual(qualifier);
  } else if (isa<ast::SelfType>(ast_type)) {
    if (parent != nullptr)
      return *parent;
  }

  throw std::runtime_error("Cannot convert AST type to actual type! "s + ast_type.kind_name() + " (" +
                           ast_type.describe() + ")");
}

void TypeWalker::resolve_queue() {
  while (!decl_queue.empty()) {
    auto next = decl_queue.front();
    decl_queue.pop();

    with_saved_scope([&] {
      next.visit_decl(
          [&](Fn* fn) {
            in_depth = true;
            compiler.declare(*fn);
          },
          [&](Struct* st) {
            for (auto& i : st->body().body()) {
              auto decl = compiler.decl_statement(i, st->self_ty, st->member);
              compiler.walk_types(decl);
            }
          },
          [&](Ctor* /*ctor*/) { throw std::runtime_error("Unimplemented"); });
    });
  }
}
} // namespace yume::semantic
