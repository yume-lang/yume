#include "type_walker.hpp"
#include "../ast.hpp"
#include "compiler.hpp"

namespace yume {

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

template <> void TypeWalker::expression(ast::Type& expr) {
  auto* resolved_type = &m_compiler.convert_type(expr, m_current_fn->parent());
  expr.val_ty(resolved_type);
  if (expr.kind() == ast::QualTypeKind) {
    expression(dynamic_cast<ast::QualType&>(expr).base());
  }
}

template <> void TypeWalker::expression(ast::TypeName& expr) {
  auto& type = expr.type();
  type.add_observer(&expr);
  expression(type);
}

template <> void TypeWalker::statement(ast::Compound& stat) {
  for (auto& i : stat.body()) {
    body_statement(i);
  }
}

template <> void TypeWalker::statement(ast::FnDecl& stat) {
  for (auto& i : stat.args()) {
    expression(i);
  }

  if (stat.ret().has_value()) {
    expression(stat.ret()->get());
  }

  if (m_in_depth && stat.body().index() == 0) {
    statement(*get<0>(stat.body()));
  }
}

// TODO: temporary? This is a pretty nasty RTTI dependence on something that should be called "often"?
auto TypeWalker::visit(ast::AST& expr, const char* label) -> TypeWalker& {
  if (auto* stmt = dynamic_cast<ast::Stmt*>(&expr); stmt != nullptr) {
    body_statement(*stmt);
  }
  return *this;
}

/*
void TypeWalkVisitor::expression(ast::AST& expr, [[maybe_unused]] const char* label) {
  if (expr.kind() == ast::CtorKind) {
    auto& ctor = dynamic_cast<ast::CtorExpr&>(expr);
    ctor.val_ty(ctor.name() == "self" ? m_current_fn->parent() : &m_compiler.known_type(ctor.name()));
  }
  if (expr.kind() == ast::AssignKind) {
    auto& assign = dynamic_cast<ast::AssignExpr&>(expr);
    assign.value().add_observer(&assign.target());
  }
  expr.visit(*this);
  return *this;
}
*/

} // namespace yume
