//
// Created by rymiel on 5/8/22.
//

#pragma once

#include "token.hpp"
#include "visitor.hpp"
#include <cstdint>
#include <ranges>
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
  QualType,
  TypeName,
  Compound,
  WhileStatement,
  IfStatement,
  IfClause,
  Call,
  Var,
  ExprStatement,
  ReturnStatement,
  Assign,
  FieldAccess,
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
  case Kind::QualType: return "qual type";
  case Kind::TypeName: return "type name";
  case Kind::Compound: return "compound";
  case Kind::WhileStatement: return "while statement";
  case Kind::IfStatement: return "if statement";
  case Kind::IfClause: return "if clause";
  case Kind::Call: return "call";
  case Kind::Var: return "var";
  case Kind::ExprStatement: return "expr statement";
  case Kind::ReturnStatement: return "return statement";
  case Kind::Assign: return "assign";
  case Kind::FieldAccess: return "field access";
  default: return "?";
  }
}

using VectorTokenIterator = vector<Token>::iterator;

struct TokenIterator {
  VectorTokenIterator m_iterator;
  VectorTokenIterator m_end;

  inline constexpr TokenIterator(const VectorTokenIterator& iterator, const VectorTokenIterator& end)
      : m_iterator{iterator}, m_end{end} {}

  [[nodiscard]] constexpr auto end() const noexcept -> bool { return m_iterator == m_end; }
  [[nodiscard]] auto constexpr operator->() const noexcept -> Token* { return m_iterator.operator->(); }
  [[nodiscard]] constexpr auto operator*() const noexcept -> Token { return m_iterator.operator*(); }
  [[nodiscard]] constexpr auto operator+(int i) const noexcept -> TokenIterator {
    return TokenIterator{m_iterator + i, m_end};
  }
  constexpr auto operator++() -> TokenIterator& {
    if (end()) {
      throw std::runtime_error("Can't increment past end");
    }
    ++m_iterator;
    return *this;
  }
  auto operator++(int) -> TokenIterator {
    if (end()) {
      throw std::runtime_error("Can't increment past end");
    }
    return TokenIterator{m_iterator++, m_end};
  }
  [[nodiscard]] auto begin() const -> VectorTokenIterator { return m_iterator; }
};

class Expr {
protected:
  Expr() = default;

public:
  virtual void inline visit(Visitor& visitor) const = 0;
  [[nodiscard]] virtual auto kind() const -> Kind = 0;
  [[nodiscard]] virtual auto inline describe() const -> string { return string{"unknown "} + kind_name(kind()); }
  virtual ~Expr() = default;

  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Expr>;
};

class Type : public Expr {
public:
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Type>;
  [[nodiscard]] static auto try_parse(TokenIterator& tokens) -> optional<unique_ptr<Type>>;
};

class SimpleType : public Type, public AST {
  string m_name;

public:
  explicit inline SimpleType(string name) : m_name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::SimpleType; }
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] constexpr auto inline name() const -> string { return m_name; }
};

class QualType : public Type, public AST {
public:
  enum struct Qualifier { Ptr, Slice };

private:
  unique_ptr<Type> m_base;
  Qualifier m_qualifier;

public:
  explicit inline QualType(unique_ptr<Type> base, Qualifier qualifier) : m_base{move(base)}, m_qualifier{qualifier} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::QualType; }
  [[nodiscard]] inline auto describe() const -> string override {
    switch (m_qualifier) {
    case Qualifier::Ptr: return "ptr";
    case Qualifier::Slice: return "slice";
    default: return "";
    }
  }

  [[nodiscard]] constexpr auto inline qualifier() const -> Qualifier { return m_qualifier; }
  [[nodiscard]] auto inline base() const -> const auto& { return *m_base; }
};

class TypeName : public Expr, public AST {
  unique_ptr<Type> m_type;
  string m_name;

public:
  inline TypeName(unique_ptr<Type>& type, string name) : m_type{move(type)}, m_name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<TypeName>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::TypeName; }
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline type() const -> const auto& { return *m_type; }
  [[nodiscard]] constexpr auto inline name() const -> string { return m_name; }

  template <size_t I> [[maybe_unused]] auto get() & -> auto& {
    if constexpr (I == 0) {
      return *m_type;
    } else if constexpr (I == 1) {
      return m_name;
    }
  }

  template <size_t I> [[maybe_unused]] auto get() const& -> auto const& {
    if constexpr (I == 0) {
      return *m_type;
    } else if constexpr (I == 1) {
      return m_name;
    }
  }

  template <size_t I> [[maybe_unused]] auto get() && -> auto&& {
    if constexpr (I == 0) {
      return std::move(*m_type);
    } else if constexpr (I == 1) {
      return std::move(m_name);
    }
  }
};

class NumberExpr : public Expr, public AST {
  int64_t m_val;

public:
  explicit inline NumberExpr(int64_t val) : m_val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<NumberExpr>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Number; }
  [[nodiscard]] inline auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] inline auto val() const -> int64_t { return m_val; }
};

class StringExpr : public Expr, public AST {
  string m_val;

public:
  explicit inline StringExpr(string val) : m_val(move(val)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<StringExpr>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::String; }
  [[nodiscard]] inline auto describe() const -> string override { return m_val; }

  [[nodiscard]] constexpr auto inline val() const -> string { return m_val; }
};

class VarExpr : public Expr, public AST {
  string m_name;

public:
  explicit inline VarExpr(string name) : m_name(move(name)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Var; }
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] constexpr auto inline name() const -> string { return m_name; }
};

class CallExpr : public Expr, public AST {
  string m_name;
  vector<unique_ptr<Expr>> m_args;

public:
  inline CallExpr(string name, vector<unique_ptr<Expr>>& args) : m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Call; }
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] constexpr auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
};

class AssignExpr : public Expr, public AST {
  unique_ptr<Expr> m_target;
  unique_ptr<Expr> m_value;

public:
  inline AssignExpr(unique_ptr<Expr>& target, unique_ptr<Expr>& value)
      : m_target{move(target)}, m_value{move(value)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Assign; }

  [[nodiscard]] auto inline target() const -> const auto& { return *m_target; }
  [[nodiscard]] auto inline value() const -> const auto& { return *m_value; }
};

class FieldAccessExpr : public Expr, public AST {
  unique_ptr<Expr> m_base;
  string m_field;

public:
  inline FieldAccessExpr(unique_ptr<Expr>& base, string field) : m_base{move(base)}, m_field{move(field)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::FieldAccess; }
};

class Statement : public Expr {
public:
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Statement>;
};

class Compound : public Statement, public AST {
  vector<unique_ptr<Statement>> m_body;

public:
  void visit(Visitor& visitor) const override;
  explicit inline Compound(vector<unique_ptr<Statement>>& body) : m_body{move(body)} {}
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Compound; }

  [[nodiscard]] auto inline body() const { return dereference_view(m_body); }
};

class FnDeclStatement : public Statement, public AST {
  string m_name;
  bool m_varargs{};
  vector<unique_ptr<TypeName>> m_args;
  vector<string> m_type_args;
  optional<unique_ptr<Type>> m_ret;
  variant<unique_ptr<Compound>, string> m_body;

public:
  inline FnDeclStatement(string name, vector<unique_ptr<TypeName>>& args, vector<string>& type_args,
                         optional<unique_ptr<Type>> ret, unique_ptr<Compound> body)
      : m_name{move(name)}, m_args{move(args)}, m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(body)} {}
  inline FnDeclStatement(string name, vector<unique_ptr<TypeName>>& args, vector<string>& type_args,
                         optional<unique_ptr<Type>> ret, bool varargs, string primitive)
      : m_name{move(name)}, m_varargs{varargs}, m_args{move(args)}, m_type_args{move(type_args)}, m_ret{move(ret)},
        m_body{move(primitive)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<FnDeclStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::FnDecl; }
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] constexpr auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
  [[nodiscard]] constexpr auto inline ret() const { return try_dereference(m_ret); }
  [[nodiscard]] constexpr auto inline body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto inline varargs() const -> bool { return m_varargs; }
  [[nodiscard]] constexpr auto inline primitive() const -> bool { return holds_alternative<string>(m_body); }
};

class VarDeclStatement : public Statement, public AST {
  string m_name;
  optional<unique_ptr<Type>> m_type;
  unique_ptr<Expr> m_init;

public:
  inline VarDeclStatement(string name, optional<unique_ptr<Type>> type, unique_ptr<Expr> init)
      : m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<VarDeclStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::VarDecl; }
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] constexpr auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline type() const { return try_dereference(m_type); }
  [[nodiscard]] auto inline init() const -> const auto& { return *m_init; }
};

class WhileStatement : public Statement, public AST {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline WhileStatement(unique_ptr<Expr> cond, unique_ptr<Compound> body) : m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<WhileStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::WhileStatement; }

  [[nodiscard]] inline auto body() const -> const auto& { return *m_body; }
  [[nodiscard]] inline auto cond() const -> const auto& { return *m_cond; }
};

class IfClause : public Expr, public AST {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline IfClause(unique_ptr<Expr> cond, unique_ptr<Compound> body) : m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::IfClause; }

  [[nodiscard]] inline auto cond() const -> const auto& { return *m_cond; }
  [[nodiscard]] inline auto body() const -> const auto& { return *m_body; }
};

class IfStatement : public Statement, public AST {
  vector<unique_ptr<IfClause>> m_clauses;
  optional<unique_ptr<Compound>> m_else_clause;

public:
  inline IfStatement(vector<unique_ptr<IfClause>>& clauses, optional<unique_ptr<Compound>> else_clause)
      : m_clauses{move(clauses)}, m_else_clause{move(else_clause)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<IfStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::IfStatement; }

  [[nodiscard]] inline auto clauses() const { return dereference_view(m_clauses); }
  [[nodiscard]] inline auto else_clause() const { return try_dereference(m_else_clause); }
};

class ReturnStatement : public Statement, public AST {
  optional<unique_ptr<Expr>> m_expr;

public:
  explicit inline ReturnStatement(optional<unique_ptr<Expr>> expr) : m_expr{move(expr)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<ReturnStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::ReturnStatement; }

  [[nodiscard]] inline auto expr() const { return try_dereference(m_expr); }
};

class ExprStatement : public Statement, public AST {
  unique_ptr<Expr> m_expr;

public:
  explicit inline ExprStatement(unique_ptr<Expr> expr) : m_expr{move(expr)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<ExprStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::ExprStatement; }

  [[nodiscard]] inline auto expr() const -> const auto& { return *m_expr; }
};

class Program : public Statement, public AST {
  vector<unique_ptr<Statement>> m_body;

public:
  explicit inline Program(vector<unique_ptr<Statement>>& body) : m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;
  [[nodiscard]] constexpr auto inline kind() const -> Kind override { return Kind::Program; }

  [[nodiscard]] constexpr auto inline body() const { return dereference_view(m_body); }
};
} // namespace yume::ast

namespace std {
template <> struct tuple_size<yume::ast::TypeName> : std::integral_constant<size_t, 2> {};

template <> struct tuple_element<0, yume::ast::TypeName> { using type = yume::ast::Type; };
template <> struct tuple_element<1, yume::ast::TypeName> { using type = std::string; };
} // namespace std
