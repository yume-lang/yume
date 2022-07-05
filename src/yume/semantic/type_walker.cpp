#include "type_walker.hpp"
#include "ast/ast.hpp"
#include "compatibility.hpp"
#include "compiler/compiler.hpp"
#include "compiler/type_holder.hpp"
#include "compiler/vals.hpp"
#include "diagnostic/errors.hpp"
#include "type.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringMapEntry.h>
#include <llvm/ADT/iterator.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace yume::semantic {

template <> void TypeWalker::expression(ast::NumberExpr& expr) {
  auto val = expr.val();
  if (val > std::numeric_limits<int32_t>::max()) {
    expr.val_ty(m_compiler.m_types.int64().s_ty);
  } else {
    expr.val_ty(m_compiler.m_types.int32().s_ty);
  }
}

template <> void TypeWalker::expression(ast::StringExpr& expr) {
  // TODO: String type
  expr.val_ty(&m_compiler.m_types.int8().u_ty->known_ptr());
}

template <> void TypeWalker::expression(ast::CharExpr& expr) { expr.val_ty(m_compiler.m_types.int8().u_ty); }

template <> void TypeWalker::expression(ast::BoolExpr& expr) { expr.val_ty(m_compiler.m_types.bool_type); }

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
  for (auto& i : expr.args()) {
    body_expression(i);
  }
  expression(expr.type());
  const auto& base_type = convert_type(expr.type());
  expr.val_ty(&base_type);
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
  if (!m_scope.contains(expr.name()))
    throw std::runtime_error("Scope doesn't contain variable called "s + expr.name());
  expr.attach_to(m_scope.at(expr.name()));
}

template <> void TypeWalker::expression(ast::FieldAccessExpr& expr) {
  body_expression(expr.base());
  const auto& type = *expr.base().val_ty();

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

auto TypeWalker::all_overloads_by_name(ast::CallExpr& call) -> OverloadSet {
  auto fns_by_name = vector<Overload>();

  for (auto& fn : m_compiler.m_fns)
    if (fn.name() == call.name())
      fns_by_name.emplace_back(&fn);

  return OverloadSet{call, fns_by_name, {}};
}

template <> void TypeWalker::expression(ast::CallExpr& expr) {
  resolve_queue();

  auto overload_set = all_overloads_by_name(expr);
  auto name = expr.name();

  if (overload_set.empty())
    throw std::logic_error("No function overload named "s + name);

  for (auto& i : expr.args()) {
    body_expression(i);
    overload_set.arg_types.push_back(i.val_ty());
  }

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\n*** BEGIN OVERLOAD EVALUATION ***\n";
  errs() << "Functions with matching names:\n";
  overload_set.dump(errs());
#endif

  overload_set.determine_valid_overloads();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\nViable overloads:\n";
  overload_set.dump(errs(), true);
#endif

  Overload best_overload = overload_set.best_viable_overload();

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\nSelected overload:\n";
  best_overload.dump(errs());
  errs() << "\n*** END OVERLOAD EVALUATION ***\n\n";
#endif

  auto& instantiate = best_overload.instantiation;
  auto& selected = best_overload.fn;

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
        m_in_depth = false;
        m_current_fn = &new_fn;
        body_statement(new_fn.ast());
      });

      m_decl_queue.push(&new_fn);

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
  for (auto& i : stat.fields()) {
    expression(i);
  }
}

template <> void TypeWalker::statement(ast::FnDecl& stat) {
  m_scope.clear();
  for (auto& i : stat.args()) {
    expression(i);
    m_scope.insert({i.name(), &i});
  }

  if (stat.ret().has_value()) {
    expression(stat.ret()->get());
    stat.attach_to(&stat.ret()->get());
  }

  // This decl still has unsubstituted generics, can't instantiate its body
  if (std::any_of(m_current_fn->m_subs.begin(), m_current_fn->m_subs.end(),
                  [&](const auto& sub) { return isa<ty::Generic>(sub.second); })) {
    return;
  }

  if (m_in_depth && stat.body().index() == 0)
    statement(get<0>(stat.body()));
}

template <> void TypeWalker::statement(ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    body_expression(stat.expr()->get());
    m_current_fn->m_ast_decl.attach_to(&stat.expr()->get());
  }
}

template <> void TypeWalker::statement(ast::VarDecl& stat) {
  body_expression(stat.init());
  if (stat.type().has_value()) {
    expression(stat.type()->get());
    stat.init().attach_to(&stat.type()->get());
  }

  stat.val_ty(&stat.init().val_ty()->known_mut());
  m_scope.insert({stat.name(), &stat});
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
  const ty::Type* parent = m_current_fn == nullptr ? m_current_struct : m_current_fn->parent();
  Fn* context = m_current_fn;

  if (const auto* simple_type = dyn_cast<ast::SimpleType>(&ast_type)) {
    auto name = simple_type->name();
    if (context != nullptr) {
      auto generic = context->m_subs.find(name);
      if (generic != context->m_subs.end())
        return *generic->second;
    }
    auto val = m_compiler.m_types.known.find(name);
    if (val != m_compiler.m_types.known.end())
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
  while (!m_decl_queue.empty()) {
    auto* next = m_decl_queue.front();
    m_decl_queue.pop();

    with_saved_scope([&] {
      m_in_depth = true;

      m_compiler.declare(*next);
    });
  }
}
} // namespace yume::semantic
