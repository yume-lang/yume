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

enum struct Kind { Unknown, Number, Decl, Program, SimpleType, TypeName, Compound };

auto inline constexpr kind_name(Kind type) -> const char* {
  switch (type) {
  case Kind::Unknown: return "unknown";
  case Kind::Number: return "number";
  case Kind::Decl: return "decl";
  case Kind::Program: return "program";
  case Kind::SimpleType: return "simple type";
  case Kind::TypeName: return "type name";
  case Kind::Compound: return "compound";
  }
}

using TokenIterator = vector<Token>::iterator;

class Expr {
protected:
  Expr() = default;

public:
  virtual void inline visit(Visitor& visitor) const {};
  [[nodiscard]] virtual auto kind() const -> Kind = 0;
  [[nodiscard]] virtual auto inline describe() const -> string { return std::string{"unknown "} + kind_name(kind()); };

  virtual ~Expr() = default;
};

class Type : public Expr {};

class SimpleType : public Type, public AST {
  string m_name;

private:
  explicit inline SimpleType(string name) : m_name{std::move(name)} {};

public:
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<SimpleType>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::SimpleType; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class TypeName : public Expr, public AST {
  unique_ptr<Type> m_type;
  string m_name;

private:
  explicit inline TypeName(unique_ptr<Type>& type, string name) : m_type{std::move(type)}, m_name{std::move(name)} {};

public:
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<TypeName>;
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::TypeName; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class NumberExpr : public Expr, public AST {
  int64_t m_val;

public:
  explicit inline NumberExpr(int64_t val) : m_val(val) {}
  [[nodiscard]] auto inline kind() const -> Kind override { return Kind::Number; };
};

class Statement : public Expr {
public:
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Statement>;
};

class Compound : public Statement, public AST {
  vector<unique_ptr<Statement>> m_body;

public:
  void visit(Visitor& visitor) const override;
  explicit inline Compound(vector<unique_ptr<Statement>>& body) : m_body{std::move(body)} {};
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Compound; };
};

class DeclStatement : public Statement, public AST {
  string m_name;
  vector<unique_ptr<TypeName>> m_args;
  unique_ptr<Type> m_ret;
  unique_ptr<Compound> m_body;

private:
  inline DeclStatement(string name, vector<unique_ptr<TypeName>>& args, unique_ptr<Type> ret, unique_ptr<Compound>& body)
      : m_name{std::move(name)}, m_args{std::move(args)}, m_ret{std::move(ret)}, m_body{std::move(body)} {};

public:
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<DeclStatement>;
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Decl; };
  [[nodiscard]] inline auto describe() const -> string override { return m_name; };
};

class Program : public Statement, public AST {
  vector<unique_ptr<Statement>> m_body;

private:
  explicit inline Program(vector<unique_ptr<Statement>>& body) : m_body{std::move(body)} {}

public:
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;
  [[nodiscard]] auto inline body() const -> const vector<unique_ptr<Statement>>& { return m_body; }
  [[nodiscard]] constexpr auto inline kind() const -> Kind override { return Kind::Program; };
};
} // namespace yume::ast
