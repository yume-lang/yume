#pragma once

#include "ast/ast.hpp"
#include <llvm/Support/Casting.h>
#include <type_traits>

namespace yume {

template <typename T>
concept OnlyStmt = std::derived_from<T, ast::Stmt> && !std::derived_from<T, ast::Expr>;

/// A helper template to walk the Abstract Syntax Tree (AST), utilizing the Curiously Recurring Template Pattern (CRTP).
/**
 * A class inheriting from `CRTPWalker` must declare the methods `body_expression` and `body_statement`, which take the
 * base classes `Expr` and `Stmt` respectively. Those methods should delegate to the same methods defined in the
 * `CRTPWalker`.
 * Then, the class must define any number of template methods named `expression` and `statement`, which take a subclass
 * of `Expr` and `Stmt` respectively. The way this is done in `Compiler` and `TypeWalker` is to define a template base
 * in the header, with a fallback implementation (which should usually throw) for any cases that are unhandled. Then,
 * for each AST node subclass that the deriving class knows how to handle, define a template specialization taking that
 * specific subclass in the implementation file.
 */
template <typename Derived> struct CRTPWalker {

public:
  auto body_statement(ast::Stmt& stat, auto&&... args) {
    auto kind = stat.kind();
    switch (kind) {
    case ast::K_Compound: return conv_statement<ast::Compound>(stat, args...);
    case ast::K_While: return conv_statement<ast::WhileStmt>(stat, args...);
    case ast::K_If: return conv_statement<ast::IfStmt>(stat, args...);
    case ast::K_Return: return conv_statement<ast::ReturnStmt>(stat, args...);
    case ast::K_VarDecl: return conv_statement<ast::VarDecl>(stat, args...);
    case ast::K_ConstDecl: return conv_statement<ast::ConstDecl>(stat, args...);
    case ast::K_FnDecl: return conv_statement<ast::FnDecl>(stat, args...);
    case ast::K_StructDecl: return conv_statement<ast::StructDecl>(stat, args...);
    case ast::K_CtorDecl: return conv_statement<ast::CtorDecl>(stat, args...);
    default: (static_cast<Derived*>(this))->body_expression(llvm::cast<ast::Expr>(stat), args...);
    }
  }

  auto body_expression(ast::Expr& expr, auto&&... args) {
    auto kind = expr.kind();
    switch (kind) {
    case ast::K_Number: return conv_expression<ast::NumberExpr>(expr, args...);
    case ast::K_String: return conv_expression<ast::StringExpr>(expr, args...);
    case ast::K_Char: return conv_expression<ast::CharExpr>(expr, args...);
    case ast::K_Bool: return conv_expression<ast::BoolExpr>(expr, args...);
    case ast::K_Call: return conv_expression<ast::CallExpr>(expr, args...);
    case ast::K_BinaryLogic: return conv_expression<ast::BinaryLogicExpr>(expr, args...);
    case ast::K_Var: return conv_expression<ast::VarExpr>(expr, args...);
    case ast::K_Const: return conv_expression<ast::ConstExpr>(expr, args...);
    case ast::K_Lambda: return conv_expression<ast::LambdaExpr>(expr, args...);
    case ast::K_Assign: return conv_expression<ast::AssignExpr>(expr, args...);
    case ast::K_Ctor: return conv_expression<ast::CtorExpr>(expr, args...);
    // case ast::K_Dtor: return conv_expression<ast::DtorExpr>(expr, args...);
    case ast::K_Slice: return conv_expression<ast::SliceExpr>(expr, args...);
    case ast::K_FieldAccess: return conv_expression<ast::FieldAccessExpr>(expr, args...);
    case ast::K_ImplicitCast: return conv_expression<ast::ImplicitCastExpr>(expr, args...);
    case ast::K_TypeExpr: return conv_expression<ast::TypeExpr>(expr, args...);
    default: return (static_cast<Derived*>(this))->template expression(expr, args...);
    }
  }

private:
  template <std::derived_from<ast::Expr> T> auto conv_expression(ast::Expr& expr, auto... args) {
    return (static_cast<Derived*>(this))->expression(llvm::cast<T>(expr), args...);
  }

  template <OnlyStmt T> void conv_statement(ast::Stmt& stat, auto... args) {
    return (static_cast<Derived*>(this))->statement(llvm::cast<T>(stat), args...);
  }
};
} // namespace yume
