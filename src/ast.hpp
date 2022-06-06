//
// Created by rymiel on 5/8/22.
//

#pragma once

#include "token.hpp"
#include "type.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace yume {
class Visitor;
}

namespace yume::ast {

enum struct Kind {
  UnknownKind,
  NumberKind,
  StringKind,
  CharKind,
  FnDeclKind,
  VarDeclKind,
  StructDeclKind,
  ProgramKind,
  SimpleTypeKind,
  QualTypeKind,
  SelfTypeKind,
  TypeNameKind,
  CompoundKind,
  WhileKind,
  IfKind,
  IfClauseKind,
  CallKind,
  CtorKind,
  VarKind,
  ReturnKind,
  AssignKind,
  FieldAccessKind,
};
using enum Kind;

auto inline constexpr kind_name(Kind type) -> const char* {
  switch (type) {
  case UnknownKind: return "unknown";
  case NumberKind: return "number";
  case StringKind: return "string";
  case CharKind: return "char";
  case FnDeclKind: return "fn decl";
  case VarDeclKind: return "var decl";
  case StructDeclKind: return "struct decl";
  case ProgramKind: return "program";
  case SimpleTypeKind: return "simple type";
  case QualTypeKind: return "qual type";
  case SelfTypeKind: return "self type";
  case TypeNameKind: return "type name";
  case CompoundKind: return "compound";
  case WhileKind: return "while statement";
  case IfKind: return "if statement";
  case IfClauseKind: return "if clause";
  case CallKind: return "call";
  case CtorKind: return "constructor";
  case VarKind: return "var";
  case ReturnKind: return "return statement";
  case AssignKind: return "assign";
  case FieldAccessKind: return "field access";
  default: return "?";
  }
}

using VectorTokenIterator = vector<Token>::iterator;

struct TokenIterator {
  VectorTokenIterator m_iterator;
  VectorTokenIterator m_end;

  inline constexpr TokenIterator(const VectorTokenIterator& iterator, const VectorTokenIterator& end)
      : m_iterator{iterator}, m_end{end} {}

  [[nodiscard]] constexpr auto at_end() const noexcept -> bool { return m_iterator == m_end; }
  [[nodiscard]] auto constexpr operator->() const noexcept -> Token* { return m_iterator.operator->(); }
  [[nodiscard]] constexpr auto operator*() const noexcept -> Token { return m_iterator.operator*(); }
  [[nodiscard]] constexpr auto operator+(int i) const noexcept -> TokenIterator {
    return TokenIterator{m_iterator + i, m_end};
  }
  constexpr auto operator++() -> TokenIterator& {
    if (at_end()) {
      throw std::runtime_error("Can't increment past end");
    }
    ++m_iterator;
    return *this;
  }
  auto operator++(int) -> TokenIterator {
    if (at_end()) {
      throw std::runtime_error("Can't increment past end");
    }
    return TokenIterator{m_iterator++, m_end};
  }
  [[nodiscard]] auto begin() const -> VectorTokenIterator { return m_iterator; }
};

class AST {
private:
  const Kind m_kind;
  const span<Token> m_tok;
  mutable ty::Type* m_val_ty{};
  mutable std::set<const AST*> m_attached{};

  inline void unify_val_ty(const AST* other) const {
    if (m_val_ty == other->m_val_ty) {
      return;
    }
    if (m_val_ty == nullptr) {
      m_val_ty = other->m_val_ty;
    } else if (other->m_val_ty == nullptr) {
      other->m_val_ty = m_val_ty;
    } else {
      throw std::logic_error("Conflicting types between AST nodes that are attached: `"s + m_val_ty->name() +
                             "` vs `" + other->m_val_ty->name() + "`!");
    }
  }

protected:
  // constexpr AST(Kind kind) : m_kind(kind) {}
  AST(Kind kind, span<Token> tok) : m_kind(kind), m_tok(tok) {}

public:
  AST(const AST&) = delete;
  AST(AST&&) = default;
  auto operator=(const AST&) -> AST& = delete;
  auto operator=(AST&&) -> AST& = delete;
  virtual ~AST() = default;

  virtual void inline visit(Visitor& visitor) = 0;
  [[nodiscard]] inline auto val_ty() const noexcept -> ty::Type* { return m_val_ty; }
  inline void val_ty(ty::Type* type) const {
    m_val_ty = type;
    for (const auto* i : m_attached) {
      unify_val_ty(i);
    }
  }

  inline void attach_to(const AST* other) const {
    other->m_attached.insert(this);
    m_attached.insert(other);
    unify_val_ty(other);
  }

  [[nodiscard]] auto kind() const -> Kind { return m_kind; };
  [[nodiscard]] auto token_range() const -> const span<Token>& { return m_tok; };
  [[nodiscard]] auto location() const -> Loc {
    if (m_tok.empty()) {
      return Loc{};
    }
    if (m_tok.size() == 1) {
      return m_tok[0].m_loc;
    }
    return m_tok[0].m_loc + m_tok[m_tok.size() - 1].m_loc;
  };
  [[nodiscard]] virtual auto inline describe() const -> string { return string{"unknown "} + kind_name(kind()); }
};

class Stmt : public AST {
protected:
  using AST::AST;
};

class Type : public AST {
protected:
  using AST::AST;
};

class SimpleType : public Type {
  string m_name;

public:
  explicit inline SimpleType(span<Token> tok, string name) : Type(SimpleTypeKind, tok), m_name{move(name)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
};

class QualType : public Type {
private:
  unique_ptr<Type> m_base;
  ty::Qualifier m_qualifier;

public:
  explicit inline QualType(span<Token> tok, unique_ptr<Type> base, ty::Qualifier qualifier)
      : Type(QualTypeKind, tok), m_base{move(base)}, m_qualifier{qualifier} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override {
    switch (m_qualifier) {
    case ty::Qualifier::Ptr: return "ptr";
    case ty::Qualifier::Slice: return "slice";
    case ty::Qualifier::Mut: return "mut";
    default: return "";
    }
  }

  [[nodiscard]] constexpr auto inline qualifier() const -> ty::Qualifier { return m_qualifier; }
  [[nodiscard]] auto inline base() const -> auto& { return *m_base; }
};

class SelfType : public Type {
public:
  explicit inline SelfType(span<Token> tok) : Type(SelfTypeKind, tok) {}
  inline void visit([[maybe_unused]] Visitor& visitor) override {}
  [[nodiscard]] inline auto describe() const -> string override { return "self"; }
};

class TypeName : public AST {
  unique_ptr<Type> m_type;
  string m_name;

public:
  inline TypeName(span<Token> tok, unique_ptr<Type>& type, string name)
      : AST(TypeNameKind, tok), m_type{move(type)}, m_name{move(name)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline type() const -> auto& { return *m_type; }
  [[nodiscard]] auto inline name() const -> string { return m_name; }

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

class Expr : public Stmt {
protected:
  using Stmt::Stmt;
};

class NumberExpr : public Expr {
  int64_t m_val;

public:
  explicit inline NumberExpr(span<Token> tok, int64_t val) : Expr(NumberKind, tok), m_val(val) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] inline auto val() const -> int64_t { return m_val; }
};

class CharExpr : public Expr {
  uint8_t m_val;

public:
  explicit inline CharExpr(span<Token> tok, uint8_t val) : Expr(CharKind, tok), m_val(val) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] inline auto val() const -> uint8_t { return m_val; }
};

class StringExpr : public Expr {
  string m_val;

public:
  explicit inline StringExpr(span<Token> tok, string val) : Expr(StringKind, tok), m_val(move(val)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_val; }

  [[nodiscard]] auto inline val() const -> string { return m_val; }
};

class VarExpr : public Expr {
  string m_name;

public:
  explicit inline VarExpr(span<Token> tok, string name) : Expr(VarKind, tok), m_name(move(name)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
};

class CallExpr : public Expr {
  string m_name;
  vector<unique_ptr<Expr>> m_args;

public:
  inline CallExpr(span<Token> tok, string name, vector<unique_ptr<Expr>>& args)
      : Expr(CallKind, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
};

class CtorExpr : public Expr {
  string m_name;
  vector<unique_ptr<Expr>> m_args;

public:
  inline CtorExpr(span<Token> tok, string name, vector<unique_ptr<Expr>>& args)
      : Expr(CtorKind, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
};

class AssignExpr : public Expr {
  unique_ptr<Expr> m_target;
  unique_ptr<Expr> m_value;

public:
  inline AssignExpr(span<Token> tok, unique_ptr<Expr>& target, unique_ptr<Expr>& value)
      : Expr(AssignKind, tok), m_target{move(target)}, m_value{move(value)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] auto inline target() const -> auto& { return *m_target; }
  [[nodiscard]] auto inline value() const -> auto& { return *m_value; }
};

class FieldAccessExpr : public Expr {
  unique_ptr<Expr> m_base;
  string m_field;

public:
  inline FieldAccessExpr(span<Token> tok, unique_ptr<Expr>& base, string field)
      : Expr(FieldAccessKind, tok), m_base{move(base)}, m_field{move(field)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] auto inline base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto inline field() const -> string { return m_field; }
};

class Compound : public Stmt {
  vector<unique_ptr<Stmt>> m_body;

public:
  void visit(Visitor& visitor) override;
  explicit inline Compound(span<Token> tok, vector<unique_ptr<Stmt>>& body)
      : Stmt(CompoundKind, tok), m_body{move(body)} {}

  [[nodiscard]] auto inline body() const { return dereference_view(m_body); }
};

class FnDecl : public Stmt {
  string m_name;
  bool m_varargs{};
  vector<unique_ptr<TypeName>> m_args;
  vector<string> m_type_args;
  optional<unique_ptr<Type>> m_ret;
  variant<unique_ptr<Compound>, string> m_body;

public:
  inline FnDecl(span<Token> tok, string name, vector<unique_ptr<TypeName>>& args, vector<string>& type_args,
                optional<unique_ptr<Type>> ret, unique_ptr<Compound> body)
      : Stmt(FnDeclKind, tok), m_name{move(name)}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(body)} {}
  inline FnDecl(span<Token> tok, string name, vector<unique_ptr<TypeName>>& args, vector<string>& type_args,
                optional<unique_ptr<Type>> ret, bool varargs, string primitive)
      : Stmt(FnDeclKind, tok), m_name{move(name)}, m_varargs{varargs}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(primitive)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
  [[nodiscard]] constexpr auto inline ret() const { return try_dereference(m_ret); }
  [[nodiscard]] constexpr auto inline body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto inline varargs() const -> bool { return m_varargs; }
  [[nodiscard]] constexpr auto inline primitive() const -> bool { return holds_alternative<string>(m_body); }
};

class StructDecl : public Stmt {
  string m_name;
  vector<TypeName> m_fields;
  vector<string> m_type_args;
  Compound m_body;

public:
  inline StructDecl(span<Token> tok, string name, vector<TypeName>& fields, vector<string>& type_args, Compound body)
      : Stmt(StructDeclKind, tok), m_name{move(name)}, m_fields{move(fields)}, m_type_args{move(type_args)},
        m_body(std::move(body)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline fields() const -> const auto& { return m_fields; }
  [[nodiscard]] constexpr auto inline body() const -> const auto& { return m_body; }
};

class VarDecl : public Stmt {
  string m_name;
  optional<unique_ptr<Type>> m_type;
  unique_ptr<Expr> m_init;

public:
  inline VarDecl(span<Token> tok, string name, optional<unique_ptr<Type>> type, unique_ptr<Expr> init)
      : Stmt(VarDeclKind, tok), m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline type() const { return try_dereference(m_type); }
  [[nodiscard]] auto inline init() -> auto& { return *m_init; }
  [[nodiscard]] auto inline init() const -> const auto& { return *m_init; }
};

class WhileStmt : public Stmt {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline WhileStmt(span<Token> tok, unique_ptr<Expr> cond, unique_ptr<Compound> body)
      : Stmt(WhileKind, tok), m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto body() const -> const auto& { return *m_body; }
  [[nodiscard]] inline auto cond() const -> const auto& { return *m_cond; }
};

class IfClause : public AST {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline IfClause(span<Token> tok, unique_ptr<Expr> cond, unique_ptr<Compound> body)
      : AST(IfClauseKind, tok), m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto cond() const -> const auto& { return *m_cond; }
  [[nodiscard]] inline auto body() const -> const auto& { return *m_body; }
};

class IfStmt : public Stmt {
  vector<unique_ptr<IfClause>> m_clauses;
  optional<unique_ptr<Compound>> m_else_clause;

public:
  inline IfStmt(span<Token> tok, vector<unique_ptr<IfClause>>& clauses, optional<unique_ptr<Compound>> else_clause)
      : Stmt(IfKind, tok), m_clauses{move(clauses)}, m_else_clause{move(else_clause)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto clauses() const { return dereference_view(m_clauses); }
  [[nodiscard]] inline auto else_clause() const { return try_dereference(m_else_clause); }
};

class ReturnStmt : public Stmt {
  optional<unique_ptr<Expr>> m_expr;

public:
  explicit inline ReturnStmt(span<Token> tok, optional<unique_ptr<Expr>> expr)
      : Stmt(ReturnKind, tok), m_expr{move(expr)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto expr() const { return try_dereference(m_expr); }
};

class Program : public Stmt {
  vector<unique_ptr<Stmt>> m_body;

public:
  explicit inline Program(span<Token> tok, vector<unique_ptr<Stmt>>& body)
      : Stmt(ProgramKind, tok), m_body{move(body)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;

  [[nodiscard]] constexpr auto inline body() const { return dereference_view(m_body); }
};
} // namespace yume::ast

namespace std {
template <> struct tuple_size<yume::ast::TypeName> : std::integral_constant<size_t, 2> {};

template <> struct tuple_element<0, yume::ast::TypeName> { using type = yume::ast::Type; };
template <> struct tuple_element<1, yume::ast::TypeName> { using type = std::string; };
} // namespace std
