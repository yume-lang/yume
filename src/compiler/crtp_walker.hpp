#pragma once

#include "ast/ast.hpp"
#include <llvm/Support/Casting.h>
#include <type_traits>

namespace yume {

template <bool Bool, typename Base> using maybe_const_t = std::conditional_t<Bool, std::add_const_t<Base>, Base>;

template <typename Derived, bool Const = true> struct CRTPWalker {

public:
  auto body_statement(maybe_const_t<Const, ast::Stmt>& stat, auto&&... args) {
    auto kind = stat.kind();
    switch (kind) {
    case ast::K_Compound: return conv_statement<ast::Compound>(stat, args...);
    case ast::K_While: return conv_statement<ast::WhileStmt>(stat, args...);
    case ast::K_If: return conv_statement<ast::IfStmt>(stat, args...);
    case ast::K_Return: return conv_statement<ast::ReturnStmt>(stat, args...);
    case ast::K_VarDecl: return conv_statement<ast::VarDecl>(stat, args...);
    case ast::K_FnDecl: return conv_statement<ast::FnDecl>(stat, args...);
    case ast::K_StructDecl: return conv_statement<ast::StructDecl>(stat, args...);
    default: body_expression(llvm::cast<ast::Expr>(stat), args...);
    }
  }

  auto body_expression(maybe_const_t<Const, ast::Expr>& expr, auto&&... args) {
    auto kind = expr.kind();
    switch (kind) {
    case ast::K_Number: return conv_expression<ast::NumberExpr>(expr, args...);
    case ast::K_String: return conv_expression<ast::StringExpr>(expr, args...);
    case ast::K_Char: return conv_expression<ast::CharExpr>(expr, args...);
    case ast::K_Bool: return conv_expression<ast::BoolExpr>(expr, args...);
    case ast::K_Call: return conv_expression<ast::CallExpr>(expr, args...);
    case ast::K_Var: return conv_expression<ast::VarExpr>(expr, args...);
    case ast::K_Assign: return conv_expression<ast::AssignExpr>(expr, args...);
    case ast::K_Ctor: return conv_expression<ast::CtorExpr>(expr, args...);
    case ast::K_Slice: return conv_expression<ast::SliceExpr>(expr, args...);
    case ast::K_FieldAccess: return conv_expression<ast::FieldAccessExpr>(expr, args...);
    case ast::K_ImplicitCast: return conv_expression<ast::ImplicitCastExpr>(expr, args...);
    default: return (static_cast<Derived*>(this))->template expression(expr, args...);
    }
  }

private:
  template <typename T>
  requires std::is_base_of_v<ast::Expr, T>
  auto conv_expression(maybe_const_t<Const, ast::Expr>& expr, auto... args) {
    return (static_cast<Derived*>(this))->expression(llvm::cast<T>(expr), args...);
  }

  template <typename T>
  requires std::conjunction_v<std::is_base_of<ast::Stmt, T>, std::negation<std::is_base_of<ast::Expr, T>>>
  void conv_statement(maybe_const_t<Const, ast::Stmt>& stat, auto... args) {
    return (static_cast<Derived*>(this))->statement(llvm::cast<T>(stat), args...);
  }
};
} // namespace yume
