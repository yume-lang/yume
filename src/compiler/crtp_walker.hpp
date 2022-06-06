#pragma once

#include "../ast.hpp"
#include <type_traits>

namespace yume {
template <typename Derived> struct CRTPWalker {
public:
  auto body_statement(const ast::Stmt& stat, auto&&... args) {
    auto kind = stat.kind();
    switch (kind) {
    case ast::CompoundKind: return conv_statement<ast::Compound>(stat, args...);
    case ast::WhileKind: return conv_statement<ast::WhileStmt>(stat, args...);
    case ast::IfKind: return conv_statement<ast::IfStmt>(stat, args...);
    case ast::ReturnKind: return conv_statement<ast::ReturnStmt>(stat, args...);
    case ast::VarDeclKind: return conv_statement<ast::VarDecl>(stat, args...);
    default: body_expression(dynamic_cast<const ast::Expr&>(stat), args...);
    }
  }

  auto body_expression(const ast::Expr& expr, auto&&... args) {
    auto kind = expr.kind();
    switch (kind) {
    case ast::NumberKind: return conv_expression<ast::NumberExpr>(expr, args...);
    case ast::StringKind: return conv_expression<ast::StringExpr>(expr, args...);
    case ast::CharKind: return conv_expression<ast::CharExpr>(expr, args...);
    case ast::CallKind: return conv_expression<ast::CallExpr>(expr, args...);
    case ast::VarKind: return conv_expression<ast::VarExpr>(expr, args...);
    case ast::AssignKind: return conv_expression<ast::AssignExpr>(expr, args...);
    case ast::CtorKind: return conv_expression<ast::CtorExpr>(expr, args...);
    case ast::FieldAccessKind: return conv_expression<ast::FieldAccessExpr>(expr, args...);
    default: return (static_cast<Derived*>(this))->template expression(expr, args...);
    }
  }

private:
  template <typename T>
  requires std::is_base_of_v<ast::Expr, T>
  auto conv_expression(const ast::Expr& expr, auto... args) {
    return (static_cast<Derived*>(this))->expression(dynamic_cast<const T&>(expr), args...);
  }

  template <typename T>
  requires std::conjunction_v<std::is_base_of<ast::Stmt, T>, std::negation<std::is_base_of<ast::Expr, T>>>
  void conv_statement(const ast::Stmt& stat, auto... args) {
    return (static_cast<Derived*>(this))->statement(dynamic_cast<const T&>(stat), args...);
  }
};
} // namespace yume
