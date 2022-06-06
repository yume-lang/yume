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
  expr.attach_to(&type);
  expression(type);
}

template <> void TypeWalker::expression(ast::CtorExpr& expr) {
  for (auto& i : expr.args()) {
    body_expression(i);
  }
  expr.val_ty(expr.name() == "self" ? m_current_fn->parent() : &m_compiler.known_type(expr.name()));
}

template <> void TypeWalker::expression(ast::AssignExpr& expr) {
  body_expression(expr.target());
  body_expression(expr.value());
  expr.target().attach_to(&expr.value());
}

template <> void TypeWalker::expression(ast::VarExpr& expr) {
  if (!m_scope.contains(expr.name())) {
    return; // TODO: this should be an error
  }
  expr.attach_to(m_scope.at(expr.name()));
}

template <> void TypeWalker::statement(ast::Compound& stat) {
  for (auto& i : stat.body()) {
    body_statement(i);
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
    stat.ret()->get().attach_to(&stat);
  }

  if (m_in_depth && stat.body().index() == 0) {
    statement(*get<0>(stat.body()));
  }
}

template <> void TypeWalker::statement(ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    body_expression(stat.expr()->get());
    stat.expr()->get().attach_to(&m_current_fn->m_ast_decl);
  }
}

template <> void TypeWalker::statement(ast::VarDecl& stat) {
  body_expression(stat.init());
  if (stat.type().has_value()) {
    expression(stat.type()->get());
    stat.type()->get().attach_to(&stat);
  }

  stat.init().attach_to(&stat);
  m_scope.insert({stat.name(), &stat});
}

// TODO: temporary? This is a pretty nasty RTTI dependence on something that should be called "often"?
auto TypeWalker::visit(ast::AST& expr, [[maybe_unused]] const char* label) -> TypeWalker& {
  if (auto* stmt = dynamic_cast<ast::Stmt*>(&expr); stmt != nullptr) {
    body_statement(*stmt);
  }
  return *this;
}

void TypeWalker::body_statement(ast::Stmt& stat) { return CRTPWalker::body_statement(stat); };
void TypeWalker::body_expression(ast::Expr& expr) { return CRTPWalker::body_expression(expr); };

} // namespace yume
