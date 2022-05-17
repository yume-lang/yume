//
// Created by rymiel on 5/8/22.
//

#pragma once

#include "token.hpp"
#include "visitor.hpp"
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace yume::ast {
class AST {
public:
  AST(const AST&) = delete;
  AST(const AST&&) = delete;
  auto operator=(const AST&) -> AST& = delete;
  auto operator=(const AST&&) -> AST& = delete;

protected:
  constexpr AST() = default;
  ~AST() = default;
};

enum struct Kind {
  Unknown,
  Number,
  String,
  FnDecl,
  VarDecl,
  Program,
  SimpleType,
  TypeName,
  Compound,
  WhileStatement,
  IfStatement,
  IfClause,
  Call,
  Var,
  ExprStatement,
  Assign
};

auto inline constexpr kind_name(Kind type) -> const char* {
  switch (type) {
  case Kind::Unknown: return "unknown";
  case Kind::Number: return "number";
  case Kind::String: return "string";
  case Kind::FnDecl: return "fn decl";
  case Kind::VarDecl: return "var decl";
  case Kind::Program: return "program";
  case Kind::SimpleType: return "simple type";
  case Kind::TypeName: return "type name";
  case Kind::Compound: return "compound";
  case Kind::WhileStatement: return "while statement";
  case Kind::IfStatement: return "if statement";
  case Kind::IfClause: return "if clause";
  case Kind::Call: return "call";
  case Kind::Var: return "var";
  case Kind::ExprStatement: return "expr statement";
  case Kind::Assign: return "assign";
  default: return "?";
  }
}

using TokenIterator = vector<Token>::iterator;

class Expr {
protected:
  Expr() = default;

public:
  virtual void inline visit(Visitor& visitor) const {};
  [[nodiscard]] virtual auto kind() const -> Kind = 0;
  [[nodiscard]] virtual auto inline describe() const -> string { return string{"unknown "} + kind_name(kind()); };
  virtual ~Expr() = default;

  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Expr>;
};

class Type : public Expr {};

class SimpleType : public Type, public AST {
  string m_name;

public:
  explicit inline SimpleType(string name) : m_name{move(name)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<SimpleType>;
  [[nodiscard]] static auto try_parse(TokenIterator& tokens) -> optional<unique_ptr<SimpleType>>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::SimpleType; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class TypeName : public Expr, public AST {
  unique_ptr<Type> m_type;
  string m_name;

public:
  inline TypeName(unique_ptr<Type>& type, string name) : m_type{move(type)}, m_name{move(name)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<TypeName>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::TypeName; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class NumberExpr : public Expr, public AST {
  int64_t m_val;

public:
  explicit inline NumberExpr(int64_t val) : m_val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<NumberExpr>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Number; };
  [[nodiscard]] inline auto describe() const -> string override { return std::to_string(m_val); };
};

class StringExpr : public Expr, public AST {
  string m_val;

public:
  explicit inline StringExpr(string val) : m_val(move(val)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<StringExpr>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::String; };
  [[nodiscard]] inline auto describe() const -> string override { return m_val; };
};

class VarExpr : public Expr, public AST {
  string m_name;

public:
  explicit inline VarExpr(string name) : m_name(move(name)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Var; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class CallExpr : public Expr, public AST {
  string m_name;
  vector<unique_ptr<Expr>> m_args;

public:
  inline CallExpr(string name, vector<unique_ptr<Expr>>& args) : m_name{move(name)}, m_args{move(args)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Call; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class AssignExpr : public Expr, public AST {
  unique_ptr<Expr> m_target;
  unique_ptr<Expr> m_value;

public:
  inline AssignExpr(unique_ptr<Expr>& target, unique_ptr<Expr>& value)
      : m_target{move(target)}, m_value{move(value)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Assign; };
};

class Statement : public Expr {
public:
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Statement>;
};

class Compound : public Statement, public AST {
  vector<unique_ptr<Statement>> m_body;

public:
  void visit(Visitor& visitor) const override;
  explicit inline Compound(vector<unique_ptr<Statement>>& body) : m_body{move(body)} {};
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Compound; };
};

class FnDeclStatement : public Statement, public AST {
  string m_name;
  vector<unique_ptr<TypeName>> m_args;
  unique_ptr<Type> m_ret;
  unique_ptr<Compound> m_body;

public:
  inline FnDeclStatement(string name, vector<unique_ptr<TypeName>>& args, unique_ptr<Type> ret,
                         unique_ptr<Compound>& body)
      : m_name{move(name)}, m_args{move(args)}, m_ret{move(ret)}, m_body{move(body)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<FnDeclStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::FnDecl; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class VarDeclStatement : public Statement, public AST {
  string m_name;
  optional<unique_ptr<Type>> m_type;
  unique_ptr<Expr> m_init;

public:
  inline VarDeclStatement(string name, optional<unique_ptr<Type>> type, unique_ptr<Expr> init)
      : m_name{move(name)}, m_type{move(type)}, m_init(move(init)){};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<VarDeclStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::VarDecl; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class WhileStatement : public Statement, public AST {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline WhileStatement(unique_ptr<Expr> cond, unique_ptr<Compound> body) : m_cond{move(cond)}, m_body{move(body)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<WhileStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::WhileStatement; };
};

class IfClause : public Expr, public AST {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline IfClause(unique_ptr<Expr>& cond, unique_ptr<Compound>& body) : m_cond{move(cond)}, m_body{move(body)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::IfClause; };
};

class IfStatement : public Statement, public AST {
  vector<unique_ptr<IfClause>> m_clauses;
  optional<unique_ptr<Compound>> m_else_clause;

public:
  inline IfStatement(vector<unique_ptr<IfClause>>& clauses, optional<unique_ptr<Compound>> else_clause) : m_clauses{move(clauses)}, m_else_clause{move(else_clause)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<IfStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::IfStatement; };
};

class ExprStatement : public Statement, public AST {
  unique_ptr<Expr> m_expr;

public:
  explicit inline ExprStatement(unique_ptr<Expr> expr) : m_expr{move(expr)} {};
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<ExprStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::ExprStatement; };
  [[nodiscard]] inline auto expr() const -> const unique_ptr<Expr>& { return m_expr; };
};

class Program : public Statement, public AST {
  vector<unique_ptr<Statement>> m_body;

public:
  explicit inline Program(vector<unique_ptr<Statement>>& body) : m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;
  [[nodiscard]] constexpr auto inline kind() const -> Kind override { return Kind::Program; };
};
} // namespace yume::ast
