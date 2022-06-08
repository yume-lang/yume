//
// Created by rymiel on 5/8/22.
//

#pragma once

#include "token.hpp"
#include "type.hpp"
#include "util.hpp"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
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
struct Fn;
} // namespace yume

namespace yume::ast {

enum Kind {
  K_Unknown,
  K_IfClause,
  K_TypeName,

  /* subclasses of Stmt */
  /**/ K_Stmt,
  /**/ K_Program,
  /**/ K_FnDecl,
  /**/ K_VarDecl,
  /**/ K_StructDecl,
  /**/ K_Compound,
  /**/ K_While,
  /**/ K_If,
  /**/ K_Return,
  /**/ /* subclasses of Expr */
  /**/ /**/ K_Expr,
  /**/ /**/ K_Number,
  /**/ /**/ K_String,
  /**/ /**/ K_Char,
  /**/ /**/ K_Assign,
  /**/ /**/ K_Call,
  /**/ /**/ K_Ctor,
  /**/ /**/ K_Slice,
  /**/ /**/ K_Var,
  /**/ /**/ K_FieldAccess,
  /**/ /**/ K_END_Expr,
  /**/ K_END_Stmt,

  /* subclasses of Type */
  /**/ K_Type,
  /**/ K_SimpleType,
  /**/ K_QualType,
  /**/ K_SelfType,
  /**/ K_END_Type,
};

auto inline constexpr kind_name(Kind type) -> const char* {
  switch (type) {
  case K_Unknown: return "unknown";
  case K_Number: return "number";
  case K_String: return "string";
  case K_Char: return "char";
  case K_FnDecl: return "fn decl";
  case K_VarDecl: return "var decl";
  case K_StructDecl: return "struct decl";
  case K_Program: return "program";
  case K_SimpleType: return "simple type";
  case K_QualType: return "qual type";
  case K_SelfType: return "self type";
  case K_TypeName: return "type name";
  case K_Compound: return "compound";
  case K_While: return "while statement";
  case K_If: return "if statement";
  case K_IfClause: return "if clause";
  case K_Call: return "call";
  case K_Ctor: return "constructor";
  case K_Slice: return "slice";
  case K_Var: return "var";
  case K_Return: return "return statement";
  case K_Assign: return "assign";
  case K_FieldAccess: return "field access";
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

class AST;

struct Attachment {
  mutable llvm::SmallPtrSet<const AST*, 2> observers{};
  mutable llvm::SmallPtrSet<const AST*, 2> depends{};
};

class AST {
protected:
  const Kind m_kind;
  const span<Token> m_tok;
  mutable const ty::Type* m_val_ty{};
  unique_ptr<Attachment> m_attach{std::make_unique<Attachment>()};

  inline void unify_val_ty() const {
    for (const auto* other : m_attach->depends) {
      if (m_val_ty == other->m_val_ty || other->m_val_ty == nullptr) {
        return;
      }
      if (m_val_ty == nullptr) {
        m_val_ty = other->m_val_ty;
      } else {
        const auto* merged = m_val_ty->coalesce(*other->m_val_ty);
        if (merged == nullptr) {
          throw std::logic_error("Conflicting types between AST nodes that are attached: `"s + m_val_ty->name() +
                                 "` vs `" + other->m_val_ty->name() + "`!");
        }
        m_val_ty = merged;
      }
    }
  }

  AST(Kind kind, span<Token> tok) : m_kind(kind), m_tok(tok) {}

public:
  AST(const AST&) = delete;
  AST(AST&&) = default;
  auto operator=(const AST&) -> AST& = delete;
  auto operator=(AST&&) -> AST& = delete;
  virtual ~AST() = default;

  virtual void inline visit(Visitor& visitor) = 0;
  [[nodiscard]] inline auto val_ty() const noexcept -> const ty::Type* { return m_val_ty; }
  inline void val_ty(const ty::Type* type) const {
    m_val_ty = type;
    for (const auto* i : m_attach->observers) {
      i->unify_val_ty();
    }
  }

  inline void attach_to(const AST* other) const {
    other->m_attach->observers.insert(this);
    this->m_attach->depends.insert(other);
    unify_val_ty();
  }

  [[nodiscard]] auto kind() const -> Kind { return m_kind; };
  [[nodiscard]] auto kind_name() const -> string { return ast::kind_name(kind()); };
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
  [[nodiscard]] virtual auto inline describe() const -> string { return string{"unknown "} + kind_name(); }
  [[nodiscard]] virtual auto clone() const -> AST* = 0;
};

class Stmt : public AST {
protected:
  using AST::AST;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Stmt && a->kind() <= K_END_Stmt; }
  [[nodiscard]] auto clone() const -> Stmt* override = 0;
};

class Type : public AST {
protected:
  using AST::AST;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Type && a->kind() <= K_END_Type; }
  [[nodiscard]] auto clone() const -> Type* override = 0;
};

class SimpleType : public Type {
  string m_name;

public:
  explicit inline SimpleType(span<Token> tok, string name) : Type(K_SimpleType, tok), m_name{move(name)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_SimpleType; }
  [[nodiscard]] auto clone() const -> SimpleType* override;
};

class QualType : public Type {
  unique_ptr<Type> m_base;
  Qualifier m_qualifier;

public:
  explicit inline QualType(span<Token> tok, unique_ptr<Type> base, Qualifier qualifier)
      : Type(K_QualType, tok), m_base{move(base)}, m_qualifier{qualifier} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override {
    switch (m_qualifier) {
    case Qualifier::Ptr: return "ptr";
    case Qualifier::Slice: return "slice";
    case Qualifier::Mut: return "mut";
    default: return "";
    }
  }

  [[nodiscard]] constexpr auto inline qualifier() const -> Qualifier { return m_qualifier; }
  [[nodiscard]] auto inline base() const -> auto& { return *m_base; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_QualType; }
  [[nodiscard]] auto clone() const -> QualType* override;
};

class SelfType : public Type {
public:
  explicit inline SelfType(span<Token> tok) : Type(K_SelfType, tok) {}
  inline void visit([[maybe_unused]] Visitor& visitor) override {}
  [[nodiscard]] inline auto describe() const -> string override { return "self"; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_SelfType; }
  [[nodiscard]] auto clone() const -> SelfType* override;
};

class TypeName : public AST {
  unique_ptr<Type> m_type;
  string m_name;

public:
  inline TypeName(span<Token> tok, unique_ptr<Type> type, string name)
      : AST(K_TypeName, tok), m_type{move(type)}, m_name{move(name)} {}
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
  static auto classof(const AST* a) -> bool { return a->kind() == K_TypeName; }
  [[nodiscard]] auto clone() const -> TypeName* override;
};

class Expr : public Stmt {
protected:
  using Stmt::Stmt;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Expr && a->kind() <= K_END_Expr; }
  [[nodiscard]] auto clone() const -> Expr* override = 0;
};

class NumberExpr : public Expr {
  int64_t m_val;

public:
  explicit inline NumberExpr(span<Token> tok, int64_t val) : Expr(K_Number, tok), m_val(val) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] inline auto val() const -> int64_t { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Number; }
  [[nodiscard]] auto clone() const -> NumberExpr* override;
};

class CharExpr : public Expr {
  uint8_t m_val;

public:
  explicit inline CharExpr(span<Token> tok, uint8_t val) : Expr(K_Char, tok), m_val(val) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] inline auto val() const -> uint8_t { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Char; }
  [[nodiscard]] auto clone() const -> CharExpr* override;
};

class StringExpr : public Expr {
  string m_val;

public:
  explicit inline StringExpr(span<Token> tok, string val) : Expr(K_String, tok), m_val(move(val)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_val; }

  [[nodiscard]] auto inline val() const -> string { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_String; }
  [[nodiscard]] auto clone() const -> StringExpr* override;
};

class VarExpr : public Expr {
  string m_name;

public:
  explicit inline VarExpr(span<Token> tok, string name) : Expr(K_Var, tok), m_name(move(name)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Var; }
  [[nodiscard]] auto clone() const -> VarExpr* override;
};

class CallExpr : public Expr {
  string m_name;
  vector<unique_ptr<Expr>> m_args;
  mutable Fn* m_selected_overload{};

public:
  inline CallExpr(span<Token> tok, string name, vector<unique_ptr<Expr>> args)
      : Expr(K_Call, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }

  void inline selected_overload(Fn* fn) const { m_selected_overload = fn; }
  [[nodiscard]] auto inline selected_overload() const -> Fn* { return m_selected_overload; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Call; }
  [[nodiscard]] auto clone() const -> CallExpr* override;
};

class CtorExpr : public Expr {
  string m_name;
  vector<unique_ptr<Expr>> m_args;

public:
  inline CtorExpr(span<Token> tok, string name, vector<unique_ptr<Expr>> args)
      : Expr(K_Ctor, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Ctor; }
  [[nodiscard]] auto clone() const -> CtorExpr* override;
};

class SliceExpr : public Expr {
  string m_name;
  vector<unique_ptr<Expr>> m_args;

public:
  inline SliceExpr(span<Token> tok, string name, vector<unique_ptr<Expr>> args)
      : Expr(K_Slice, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Slice; }
  [[nodiscard]] auto clone() const -> SliceExpr* override;
};

class AssignExpr : public Expr {
  unique_ptr<Expr> m_target;
  unique_ptr<Expr> m_value;

public:
  inline AssignExpr(span<Token> tok, unique_ptr<Expr> target, unique_ptr<Expr> value)
      : Expr(K_Assign, tok), m_target{move(target)}, m_value{move(value)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] auto inline target() const -> auto& { return *m_target; }
  [[nodiscard]] auto inline value() const -> auto& { return *m_value; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Assign; }
  [[nodiscard]] auto clone() const -> AssignExpr* override;
};

class FieldAccessExpr : public Expr {
  unique_ptr<Expr> m_base;
  string m_field;
  mutable int m_offset = -1;

public:
  inline FieldAccessExpr(span<Token> tok, unique_ptr<Expr> base, string field)
      : Expr(K_FieldAccess, tok), m_base{move(base)}, m_field{move(field)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] auto inline base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto inline base() -> auto& { return *m_base; }
  [[nodiscard]] auto inline field() const -> string { return m_field; }
  void inline offset(int offset) const { m_offset = offset; }
  [[nodiscard]] auto inline offset() const -> int { return m_offset; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FieldAccess; }
  [[nodiscard]] auto clone() const -> FieldAccessExpr* override;
};

class Compound : public Stmt {
  vector<unique_ptr<Stmt>> m_body;

public:
  void visit(Visitor& visitor) override;
  explicit inline Compound(span<Token> tok, vector<unique_ptr<Stmt>> body)
      : Stmt(K_Compound, tok), m_body{move(body)} {}

  [[nodiscard]] auto inline body() const { return dereference_view(m_body); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Compound; }
  [[nodiscard]] auto clone() const -> Compound* override;
};

class FnDecl : public Stmt {
  string m_name;
  bool m_varargs{};
  vector<unique_ptr<TypeName>> m_args;
  vector<string> m_type_args;
  optional<unique_ptr<Type>> m_ret;
  variant<unique_ptr<Compound>, string> m_body;

public:
  inline FnDecl(span<Token> tok, string name, vector<unique_ptr<TypeName>> args, vector<string> type_args,
                optional<unique_ptr<Type>> ret, unique_ptr<Compound> body)
      : Stmt(K_FnDecl, tok), m_name{move(name)}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(body)} {}
  inline FnDecl(span<Token> tok, string name, vector<unique_ptr<TypeName>> args, vector<string> type_args,
                optional<unique_ptr<Type>> ret, bool varargs, string primitive)
      : Stmt(K_FnDecl, tok), m_name{move(name)}, m_varargs{varargs}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(primitive)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline args() const { return dereference_view(m_args); }
  [[nodiscard]] auto inline type_args() const { return m_type_args; }
  [[nodiscard]] constexpr auto inline ret() const { return try_dereference(m_ret); }
  [[nodiscard]] constexpr auto inline body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto inline varargs() const -> bool { return m_varargs; }
  [[nodiscard]] constexpr auto inline primitive() const -> bool { return holds_alternative<string>(m_body); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FnDecl; }
  [[nodiscard]] auto clone() const -> FnDecl* override;
};

class StructDecl : public Stmt {
  string m_name;
  vector<TypeName> m_fields;
  vector<string> m_type_args;
  Compound m_body;

public:
  inline StructDecl(span<Token> tok, string name, vector<TypeName> fields, vector<string> type_args, Compound body)
      : Stmt(K_StructDecl, tok), m_name{move(name)}, m_fields{move(fields)}, m_type_args{move(type_args)},
        m_body(std::move(body)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline fields() const -> const auto& { return m_fields; }
  [[nodiscard]] constexpr auto inline body() const -> const auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_StructDecl; }
  [[nodiscard]] auto clone() const -> StructDecl* override;
};

class VarDecl : public Stmt {
  string m_name;
  optional<unique_ptr<Type>> m_type;
  unique_ptr<Expr> m_init;

public:
  inline VarDecl(span<Token> tok, string name, optional<unique_ptr<Type>> type, unique_ptr<Expr> init)
      : Stmt(K_VarDecl, tok), m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] inline auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto inline name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto inline type() const { return try_dereference(m_type); }
  [[nodiscard]] auto inline init() -> auto& { return *m_init; }
  [[nodiscard]] auto inline init() const -> const auto& { return *m_init; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_VarDecl; }
  [[nodiscard]] auto clone() const -> VarDecl* override;
};

class WhileStmt : public Stmt {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline WhileStmt(span<Token> tok, unique_ptr<Expr> cond, unique_ptr<Compound> body)
      : Stmt(K_While, tok), m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto body() const -> const auto& { return *m_body; }
  [[nodiscard]] inline auto cond() const -> const auto& { return *m_cond; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_While; }
  [[nodiscard]] auto clone() const -> WhileStmt* override;
};

class IfClause : public AST {
  unique_ptr<Expr> m_cond;
  unique_ptr<Compound> m_body;

public:
  inline IfClause(span<Token> tok, unique_ptr<Expr> cond, unique_ptr<Compound> body)
      : AST(K_IfClause, tok), m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto cond() const -> const auto& { return *m_cond; }
  [[nodiscard]] inline auto body() const -> const auto& { return *m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_IfClause; }
  [[nodiscard]] auto clone() const -> IfClause* override;
};

class IfStmt : public Stmt {
  vector<unique_ptr<IfClause>> m_clauses;
  optional<unique_ptr<Compound>> m_else_clause;

public:
  inline IfStmt(span<Token> tok, vector<unique_ptr<IfClause>> clauses, optional<unique_ptr<Compound>> else_clause)
      : Stmt(K_If, tok), m_clauses{move(clauses)}, m_else_clause{move(else_clause)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto clauses() const { return dereference_view(m_clauses); }
  [[nodiscard]] inline auto else_clause() const { return try_dereference(m_else_clause); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_If; }
  [[nodiscard]] auto clone() const -> IfStmt* override;
};

class ReturnStmt : public Stmt {
  optional<unique_ptr<Expr>> m_expr;

public:
  explicit inline ReturnStmt(span<Token> tok, optional<unique_ptr<Expr>> expr)
      : Stmt(K_Return, tok), m_expr{move(expr)} {}
  void visit(Visitor& visitor) override;

  [[nodiscard]] inline auto expr() const { return try_dereference(m_expr); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Return; }
  [[nodiscard]] auto clone() const -> ReturnStmt* override;
};

class Program : public Stmt {
  vector<unique_ptr<Stmt>> m_body;

public:
  explicit inline Program(span<Token> tok, vector<unique_ptr<Stmt>> body) : Stmt(K_Program, tok), m_body{move(body)} {}
  void visit(Visitor& visitor) override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;

  [[nodiscard]] constexpr auto inline body() const { return dereference_view(m_body); }
  [[nodiscard]] constexpr auto inline direct_body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Program; }
  [[nodiscard]] auto clone() const -> Program* override;
};
} // namespace yume::ast

namespace std {
template <> struct tuple_size<yume::ast::TypeName> : std::integral_constant<size_t, 2> {};

template <> struct tuple_element<0, yume::ast::TypeName> { using type = yume::ast::Type; };
template <> struct tuple_element<1, yume::ast::TypeName> { using type = std::string; };
} // namespace std
