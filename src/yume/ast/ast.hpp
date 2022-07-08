#pragma once

#include "qualifier.hpp"
#include "stl_util.hpp"
#include "token.hpp"
#include "ty/compatibility.hpp"
#include "util.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace yume {
class Visitor;
struct Fn;
namespace ty {
class Type;
}
} // namespace yume

namespace yume::ast {

enum Kind {
  K_Unknown,  ///< Unknown, default, zero value. Hopefully never encountered!
  K_IfClause, ///< `IfClause`
  K_TypeName, ///< `TypeName`

  /** subclasses of Stmt */
  /**/ K_Stmt,       ///< `Stmt`
  /**/ K_Compound,   ///< `Compound`
  /**/ K_FnDecl,     ///< `FnDecl`
  /**/ K_StructDecl, ///< `StructDecl`
  /**/ K_VarDecl,    ///< `VarDecl`
  /**/ K_While,      ///< `WhileStmt`
  /**/ K_If,         ///< `IfStmt`
  /**/ K_Return,     ///< `ReturnStmt`
  /**/ K_Program,    ///< `Program`

  /**** subclasses of Expr */
  /****/ K_Expr,         ///< `Expr`
  /****/ K_Number,       ///< `NumberExpr`
  /****/ K_Char,         ///< `CharExpr`
  /****/ K_Bool,         ///< `BoolExpr`
  /****/ K_String,       ///< `StringExpr`
  /****/ K_Var,          ///< `VarExpr`
  /****/ K_Call,         ///< `CallExpr`
  /****/ K_Ctor,         ///< `CtorExpr`
  /****/ K_Dtor,         ///< `DtorExpr`
  /****/ K_Slice,        ///< `SliceExpr`
  /****/ K_Assign,       ///< `AssignExpr`
  /****/ K_FieldAccess,  ///< `FieldAccessExpr`
  /****/ K_ImplicitCast, ///< `ImplicitCastExpr`
  /****/ K_END_Expr,
  /**/ K_END_Stmt,

  /** subclasses of Type */
  /**/ K_Type,          ///< `Type`
  /**/ K_SimpleType,    ///< `SimpleType`
  /**/ K_QualType,      ///< `QualType`
  /**/ K_TemplatedType, ///< `TemplatedType`
  /**/ K_SelfType,      ///< `SelfType`
  /**/ K_END_Type,
};

auto inline constexpr kind_name(Kind type) -> const char* {
  switch (type) {
  case K_Unknown: return "unknown";
  case K_Number: return "number";
  case K_String: return "string";
  case K_Char: return "char";
  case K_Bool: return "bool";
  case K_FnDecl: return "fn decl";
  case K_VarDecl: return "var decl";
  case K_StructDecl: return "struct decl";
  case K_Program: return "program";
  case K_SimpleType: return "simple type";
  case K_QualType: return "qual type";
  case K_TemplatedType: return "templated type";
  case K_SelfType: return "self type";
  case K_TypeName: return "type name";
  case K_Compound: return "compound";
  case K_While: return "while statement";
  case K_If: return "if statement";
  case K_IfClause: return "if clause";
  case K_Call: return "call";
  case K_Ctor: return "constructor";
  case K_Dtor: return "destructor";
  case K_Slice: return "slice";
  case K_Var: return "var";
  case K_Return: return "return statement";
  case K_Assign: return "assign";
  case K_FieldAccess: return "field access";
  case K_ImplicitCast: return "implicit cast";
  default: return "?";
  }
}

using VectorTokenIterator = vector<Token>::iterator;

/// An iterator-like holding `Token`s, used when parsing.
/**
 * Every parse method usually takes this as the first parameter. This is its own struct as it actually holds two
 * iterators, where one is the end. This is to provide safe indexing (avoiding going past the end) without having to
 * pass the end iterator around as a separate parameter.
 */
class TokenIterator {
  VectorTokenIterator m_iterator;
  VectorTokenIterator m_end;

public:
  constexpr TokenIterator(const VectorTokenIterator& iterator, const VectorTokenIterator& end)
      : m_iterator{iterator}, m_end{end} {}

  /// Check if the iterator is at the end and no more `Token`s could possibly be read.
  [[nodiscard]] constexpr auto at_end() const noexcept -> bool { return m_iterator == m_end; }
  [[nodiscard]] auto constexpr operator->() const -> Token* {
    if (at_end())
      throw std::runtime_error("Can't dereference at end");
    return m_iterator.operator->();
  }
  [[nodiscard]] constexpr auto operator*() const -> Token {
    if (at_end())
      throw std::runtime_error("Can't dereference at end");
    return m_iterator.operator*();
  }
  [[nodiscard]] constexpr auto operator+(int i) const noexcept -> TokenIterator {
    return TokenIterator{m_iterator + i, m_end};
  }
  constexpr auto operator++() -> TokenIterator& {
    if (at_end())
      throw std::runtime_error("Can't increment past end");

    ++m_iterator;
    return *this;
  }
  auto operator++(int) -> TokenIterator {
    if (at_end())
      throw std::runtime_error("Can't increment past end");

    return TokenIterator{m_iterator++, m_end};
  }
  /// Returns the underlying iterator. This shouldn't really be needed but I'm too lazy to properly model an iterator.
  [[nodiscard]] auto begin() const -> VectorTokenIterator { return m_iterator; }
};

class AST;

/// Represents the relationship between multiple `AST` nodes.
/**
 * `AST` nodes can be attached to propagate type information between them.
 * TODO: describe type propagation more
 */
struct Attachment {
  /// Observers are the "children" and should have their type updated when this node's type changes
  mutable llvm::SmallPtrSet<const AST*, 2> observers{};
  /// Depends are the "parents" and should be verified for type compatibility when this node's type changes
  mutable llvm::SmallPtrSet<const AST*, 2> depends{};
};

/// All nodes in the `AST` tree of the program inherit from this class.
/**
 * AST nodes cannot be copied or moved as all special member functions (aside from the destructor) are `delete`d. There
 * will always be one instance for a specific node during the lifetype of the compiler.
 * However note that AST nodes can be cloned with the `clone()` method, which will produce a deep copy. This is used
 * when instantiating a template. The clone will have the exact same structure and locations but distinct types,
 * depending on what the types the template is instantiated with.
 */
class AST {
  /// Every subclass of `AST` has a distinct `Kind`.
  const Kind m_kind;
  /// The range of tokenizer `Token`s that this node was parsed from.
  const span<Token> m_tok;
  /// The value type of this node. Determined in the semantic phase; always `nullptr` after parsing.
  mutable const ty::Type* m_val_ty{};
  /// \see Attachment
  unique_ptr<Attachment> m_attach{std::make_unique<Attachment>()};

protected:
  /// Verify the type compatibility of the depends of this node, and merge the types if possible.
  /// This is called every time the node's type is updated.
  void unify_val_ty() const;

  [[nodiscard]] auto tok() const noexcept -> span<Token> { return m_tok; }

  AST(Kind kind, span<Token> tok) : m_kind(kind), m_tok(tok) {}

public:
  AST(const AST&) = delete;
  AST(AST&&) = default;
  auto operator=(const AST&) -> AST& = delete;
  auto operator=(AST&&) -> AST& = delete;
  virtual ~AST() = default;

  virtual void visit(Visitor& visitor) const = 0;

  [[nodiscard]] auto val_ty() const noexcept -> const ty::Type* { return m_val_ty; }
  [[nodiscard]] auto get_val_ty() const noexcept -> const ty::Type* { return m_val_ty; }
  void val_ty(const ty::Type* type) const {
    m_val_ty = type;
    for (const auto* i : m_attach->observers) {
      i->unify_val_ty();
    }
  }

  /// Make the type of this node depend on the type of `other`.
  /// \sa Attachment
  void attach_to(const AST* other) const {
    other->m_attach->observers.insert(this);
    this->m_attach->depends.insert(other);
    unify_val_ty();
  }

  [[nodiscard]] auto kind() const -> Kind { return m_kind; };
  /// Human-readable string representation of the `Kind` of this node.
  [[nodiscard]] auto kind_name() const -> string { return ast::kind_name(kind()); };
  [[nodiscard]] auto token_range() const -> const span<Token>& { return m_tok; };

  /// The union of the locations of the `Token`s making up this node.
  [[nodiscard]] auto location() const -> Loc {
    if (m_tok.empty())
      return Loc{};
    if (m_tok.size() == 1)
      return m_tok[0].loc;

    return m_tok[0].loc + m_tok[m_tok.size() - 1].loc;
  };

  /// A short, string representation for debugging.
  [[nodiscard]] virtual auto describe() const -> string { return string{"unknown "} + kind_name(); }

  /// Deep copy, except for the type and attachments.
  [[nodiscard]] virtual auto clone() const -> AST* = 0;
};

/// Statements make up most things in source code.
/**
 * This includes declarations (such as function declarations) which must be at the top level of a program; or things
 * such as if statements, which must be inside the bodies of functions, but have no associated value.
 * Expressions (`Expr`) are subclasses of `Stmt`, but *do* have an associated value.
 * Note that in a future version of the language, declarations could be moved to a separate class and the distinction
 * between statements and expressions may be removed.
 */
class Stmt : public AST {
protected:
  using AST::AST;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Stmt && a->kind() <= K_END_Stmt; }
  [[nodiscard]] auto clone() const -> Stmt* override = 0;
};

/// A type **annotation**. This (`ast::Type`) is distinct from the actual type of a value (`ty::Type`).
class Type : public AST {
protected:
  using AST::AST;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Type && a->kind() <= K_END_Type; }
  [[nodiscard]] auto clone() const -> Type* override = 0;
};

/// Just the name of a type, always capitalized.
class SimpleType : public Type {
  string m_name;

public:
  explicit SimpleType(span<Token> tok, string name) : Type(K_SimpleType, tok), m_name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_SimpleType; }
  [[nodiscard]] auto clone() const -> SimpleType* override;
};

/// A type with a `Qualifier` like `mut` or `[]` following.
class QualType : public Type {
  unique_ptr<Type> m_base;
  Qualifier m_qualifier;

public:
  QualType(span<Token> tok, unique_ptr<Type> base, Qualifier qualifier)
      : Type(K_QualType, tok), m_base{move(base)}, m_qualifier{qualifier} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override {
    switch (m_qualifier) {
    case Qualifier::Ptr: return "ptr";
    case Qualifier::Slice: return "slice";
    case Qualifier::Mut: return "mut";
    default: return "";
    }
  }

  [[nodiscard]] constexpr auto qualifier() const -> Qualifier { return m_qualifier; }
  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }
  [[nodiscard]] auto direct_base() -> auto& { return m_base; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_QualType; }
  [[nodiscard]] auto clone() const -> QualType* override;
};

/// A type with explicit type parameters \e i.e. Foo<Bar,Baz>.
class TemplatedType : public Type {
  unique_ptr<Type> m_base;
  vector<unique_ptr<Type>> m_type_args;

public:
  TemplatedType(span<Token> tok, unique_ptr<Type> base, vector<unique_ptr<Type>> type_args)
      : Type(K_TemplatedType, tok), m_base{move(base)}, m_type_args{move(type_args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override {
    stringstream ss{};
    ss << "{";
    int j = 0;
    for (const auto& i : m_type_args) {
      if (j++ > 0)
        ss << ",";
      ss << i->describe();
    }
    ss << "}";
    return ss.str();
  }

  [[nodiscard]] auto type_vars() const -> const vector<unique_ptr<Type>>& { return m_type_args; }
  [[nodiscard]] auto type_vars() -> vector<unique_ptr<Type>>& { return m_type_args; }
  [[nodiscard]] auto base() const -> auto& { return *m_base; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_TemplatedType; }
  [[nodiscard]] auto clone() const -> TemplatedType* override;
};

/// The `self` type.
class SelfType : public Type {
public:
  explicit SelfType(span<Token> tok) : Type(K_SelfType, tok) {}
  void visit([[maybe_unused]] Visitor& visitor) const override {}
  [[nodiscard]] auto describe() const -> string override { return "self"; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_SelfType; }
  [[nodiscard]] auto clone() const -> SelfType* override;
};

/// A pair of a `Type` and an identifier, \e i.e. a parameter name.
class TypeName : public AST {
  unique_ptr<Type> m_type;
  string m_name;

public:
  TypeName(span<Token> tok, unique_ptr<Type> type, string name)
      : AST(K_TypeName, tok), m_type{move(type)}, m_name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto type() const -> auto& { return *m_type; }
  [[nodiscard]] auto name() const -> string { return m_name; }

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
      return move(*m_type);
    } else if constexpr (I == 1) {
      return move(m_name);
    }
  }
  static auto classof(const AST* a) -> bool { return a->kind() == K_TypeName; }
  [[nodiscard]] auto clone() const -> TypeName* override;
};

/// Expressions have an associated value and type.
class Expr : public Stmt {
protected:
  using Stmt::Stmt;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Expr && a->kind() <= K_END_Expr; }
  [[nodiscard]] auto clone() const -> Expr* override = 0;
};

/// Number literals.
class NumberExpr : public Expr {
  int64_t m_val;

public:
  explicit NumberExpr(span<Token> tok, int64_t val) : Expr(K_Number, tok), m_val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] auto val() const -> int64_t { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Number; }
  [[nodiscard]] auto clone() const -> NumberExpr* override;
};

/// Char literals.
class CharExpr : public Expr {
  uint8_t m_val;

public:
  explicit CharExpr(span<Token> tok, uint8_t val) : Expr(K_Char, tok), m_val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return std::to_string(m_val); }

  [[nodiscard]] auto val() const -> uint8_t { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Char; }
  [[nodiscard]] auto clone() const -> CharExpr* override;
};

/// Bool literals (`true` or `false`).
class BoolExpr : public Expr {
  bool m_val;

public:
  explicit BoolExpr(span<Token> tok, bool val) : Expr(K_Bool, tok), m_val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_val ? "true" : "false"; }

  [[nodiscard]] auto val() const -> bool { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Bool; }
  [[nodiscard]] auto clone() const -> BoolExpr* override;
};

/// String literals.
class StringExpr : public Expr {
  string m_val;

public:
  explicit StringExpr(span<Token> tok, string val) : Expr(K_String, tok), m_val(move(val)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_val; }

  [[nodiscard]] auto val() const -> string { return m_val; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_String; }
  [[nodiscard]] auto clone() const -> StringExpr* override;
};

/// A variable, \e i.e. just an identifier.
class VarExpr : public Expr {
  string m_name;

public:
  explicit VarExpr(span<Token> tok, string name) : Expr(K_Var, tok), m_name(move(name)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Var; }
  [[nodiscard]] auto clone() const -> VarExpr* override;
};

/// A function call or operator.
class CallExpr : public Expr {
  string m_name;
  vector<unique_ptr<Expr>> m_args;
  mutable Fn* m_selected_overload{};

public:
  CallExpr(span<Token> tok, string name, vector<unique_ptr<Expr>> args)
      : Expr(K_Call, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const { return dereference_view(m_args); }
  [[nodiscard]] auto direct_args() -> auto& { return m_args; }

  void selected_overload(Fn* fn) const { m_selected_overload = fn; }
  [[nodiscard]] auto selected_overload() const -> Fn* { return m_selected_overload; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Call; }
  [[nodiscard]] auto clone() const -> CallExpr* override;
};

/// A construction of a struct or cast of a primitive.
class CtorExpr : public Expr {
  unique_ptr<Type> m_type;
  vector<unique_ptr<Expr>> m_args;

public:
  CtorExpr(span<Token> tok, unique_ptr<Type> type, vector<unique_ptr<Expr>> args)
      : Expr(K_Ctor, tok), m_type{move(type)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_type->describe(); }

  [[nodiscard]] auto type() const -> const auto& { return *m_type; }
  [[nodiscard]] auto type() -> auto& { return *m_type; }
  [[nodiscard]] constexpr auto args() const { return dereference_view(m_args); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Ctor; }
  [[nodiscard]] auto clone() const -> CtorExpr* override;
};

/// A destruction of an object upon leaving its scope.
class [[deprecated]] DtorExpr : public Expr {
  unique_ptr<Expr> m_base;

public:
  DtorExpr(span<Token> tok, unique_ptr<Expr> base) : Expr(K_Dtor, tok), m_base{move(base)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_base->describe(); }

  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Dtor; }
  [[nodiscard]] auto clone() const -> DtorExpr* override;
};

/// A slice literal, i.e. an array.
class SliceExpr : public Expr {
  unique_ptr<Type> m_type;
  vector<unique_ptr<Expr>> m_args;

public:
  SliceExpr(span<Token> tok, unique_ptr<Type> type, vector<unique_ptr<Expr>> args)
      : Expr(K_Slice, tok), m_type{move(type)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_type->describe(); }

  [[nodiscard]] auto type() const -> const auto& { return *m_type; }
  [[nodiscard]] auto type() -> auto& { return *m_type; }
  [[nodiscard]] constexpr auto args() const { return dereference_view(m_args); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Slice; }
  [[nodiscard]] auto clone() const -> SliceExpr* override;
};

/// An assignment (`=`).
/**
 * Assignment has a specific target (the left-hand side), and the value (the right-hand-side).
 * The target may be multiple things, such as a variable `VarExpr` or field `FieldAccessExpr`.
 * Note that some things such as indexed assignment `[]=` become a `CallExpr` instead.
 */
class AssignExpr : public Expr {
  unique_ptr<Expr> m_target;
  unique_ptr<Expr> m_value;

public:
  AssignExpr(span<Token> tok, unique_ptr<Expr> target, unique_ptr<Expr> value)
      : Expr(K_Assign, tok), m_target{move(target)}, m_value{move(value)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto target() const -> auto& { return *m_target; }
  [[nodiscard]] auto value() const -> auto& { return *m_value; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Assign; }
  [[nodiscard]] auto clone() const -> AssignExpr* override;
};

/// Direct access of a field of a struct (`::`).
class FieldAccessExpr : public Expr {
  unique_ptr<Expr> m_base;
  string m_field;
  mutable int m_offset = -1;

public:
  FieldAccessExpr(span<Token> tok, unique_ptr<Expr> base, string field)
      : Expr(K_FieldAccess, tok), m_base{move(base)}, m_field{move(field)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }
  [[nodiscard]] auto field() const -> string { return m_field; }
  void offset(int offset) const { m_offset = offset; }
  [[nodiscard]] auto offset() const -> int { return m_offset; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FieldAccess; }
  [[nodiscard]] auto clone() const -> FieldAccessExpr* override;
};

class ImplicitCastExpr : public Expr {
  unique_ptr<Expr> m_base;
  ty::Conv m_conversion;

public:
  ImplicitCastExpr(span<Token> tok, unique_ptr<Expr> base, ty::Conv conversion)
      : Expr(K_ImplicitCast, tok), m_base{move(base)}, m_conversion{conversion} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }
  [[nodiscard]] auto conversion() const { return m_conversion; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_ImplicitCast; }
  [[nodiscard]] auto clone() const -> ImplicitCastExpr* override;
};

/// A statement consisting of multiple other statements, i.e. the body of a function.
class Compound : public Stmt {
  vector<unique_ptr<Stmt>> m_body;

public:
  void visit(Visitor& visitor) const override;
  explicit Compound(span<Token> tok, vector<unique_ptr<Stmt>> body) : Stmt(K_Compound, tok), m_body{move(body)} {}

  [[nodiscard]] auto body() const { return dereference_view(m_body); }
  [[nodiscard]] auto direct_body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Compound; }
  [[nodiscard]] auto clone() const -> Compound* override;
};

/// A declaration of a function (`def`).
class FnDecl : public Stmt {
  string m_name;
  bool m_varargs{};
  vector<TypeName> m_args;
  vector<string> m_type_args;
  optional<unique_ptr<Type>> m_ret;
  variant<Compound, string> m_body;

public:
  FnDecl(span<Token> tok, string name, vector<TypeName> args, vector<string> type_args, optional<unique_ptr<Type>> ret,
         Compound body)
      : Stmt(K_FnDecl, tok), m_name{move(name)}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(body)} {}
  FnDecl(span<Token> tok, string name, vector<TypeName> args, vector<string> type_args, optional<unique_ptr<Type>> ret,
         bool varargs, string primitive)
      : Stmt(K_FnDecl, tok), m_name{move(name)}, m_varargs{varargs}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(primitive)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto type_args() const { return m_type_args; }
  [[nodiscard]] constexpr auto ret() const { return try_dereference(m_ret); }
  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto body() -> auto& { return m_body; }
  [[nodiscard]] constexpr auto varargs() const -> bool { return m_varargs; }
  [[nodiscard]] constexpr auto primitive() const -> bool { return holds_alternative<string>(m_body); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FnDecl; }
  [[nodiscard]] auto clone() const -> FnDecl* override;
};

/// A declaration of a struct (`struct`).
class StructDecl : public Stmt {
  string m_name;
  vector<TypeName> m_fields;
  vector<string> m_type_args;
  Compound m_body;

public:
  StructDecl(span<Token> tok, string name, vector<TypeName> fields, vector<string> type_args, Compound body)
      : Stmt(K_StructDecl, tok), m_name{move(name)}, m_fields{move(fields)}, m_type_args{move(type_args)},
        m_body(move(body)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto fields() const -> const auto& { return m_fields; }
  [[nodiscard]] constexpr auto fields() -> auto& { return m_fields; }
  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto type_args() const { return m_type_args; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_StructDecl; }
  [[nodiscard]] auto clone() const -> StructDecl* override;
};

/// A declaration of a local variable (`let`).
class VarDecl : public Stmt {
  string m_name;
  optional<unique_ptr<Type>> m_type;
  unique_ptr<Expr> m_init;

public:
  VarDecl(span<Token> tok, string name, optional<unique_ptr<Type>> type, unique_ptr<Expr> init)
      : Stmt(K_VarDecl, tok), m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return m_name; }

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto type() const { return try_dereference(m_type); }
  [[nodiscard]] auto init() -> auto& { return *m_init; }
  [[nodiscard]] auto init() const -> const auto& { return *m_init; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_VarDecl; }
  [[nodiscard]] auto clone() const -> VarDecl* override;
};

/// A while loop (`while`).
class WhileStmt : public Stmt {
  unique_ptr<Expr> m_cond;
  Compound m_body;

public:
  WhileStmt(span<Token> tok, unique_ptr<Expr> cond, Compound body)
      : Stmt(K_While, tok), m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto cond() const -> const auto& { return *m_cond; }
  [[nodiscard]] auto cond() -> auto& { return *m_cond; }
  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_While; }
  [[nodiscard]] auto clone() const -> WhileStmt* override;
};

/// Clauses of an if statement `IfStmt`.
class IfClause : public AST {
  unique_ptr<Expr> m_cond;
  Compound m_body;

public:
  IfClause(span<Token> tok, unique_ptr<Expr> cond, Compound body)
      : AST(K_IfClause, tok), m_cond{move(cond)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto cond() const -> const auto& { return *m_cond; }
  [[nodiscard]] auto cond() -> auto& { return *m_cond; }
  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_IfClause; }
  [[nodiscard]] auto clone() const -> IfClause* override;
};

/// An if statement (`if`), with one or more `IfClause`s, and optionally an else clause.
class IfStmt : public Stmt {
  vector<IfClause> m_clauses;
  optional<Compound> m_else_clause;

public:
  IfStmt(span<Token> tok, vector<IfClause> clauses, optional<Compound> else_clause)
      : Stmt(K_If, tok), m_clauses{move(clauses)}, m_else_clause{move(else_clause)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto clauses() const -> const auto& { return m_clauses; }
  [[nodiscard]] auto clauses() -> auto& { return m_clauses; }
  [[nodiscard]] auto else_clause() const -> const auto& { return m_else_clause; }
  [[nodiscard]] auto else_clause() -> auto& { return m_else_clause; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_If; }
  [[nodiscard]] auto clone() const -> IfStmt* override;
};

/// Return from a function body.
class ReturnStmt : public Stmt {
  optional<unique_ptr<Expr>> m_expr;

public:
  explicit ReturnStmt(span<Token> tok, optional<unique_ptr<Expr>> expr) : Stmt(K_Return, tok), m_expr{move(expr)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto expr() const { return try_dereference(m_expr); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Return; }
  [[nodiscard]] auto clone() const -> ReturnStmt* override;
};

/// The top level structure of a file of source code.
class Program : public Stmt {
  vector<unique_ptr<Stmt>> m_body;

public:
  explicit Program(span<Token> tok, vector<unique_ptr<Stmt>> body) : Stmt(K_Program, tok), m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;

  [[nodiscard]] constexpr auto body() const { return dereference_view(m_body); }
  [[nodiscard]] constexpr auto direct_body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Program; }
  [[nodiscard]] auto clone() const -> Program* override;
};
} // namespace yume::ast

// these clutter the docs and are hidden from docs
/// \cond
namespace std {
template <> struct tuple_size<yume::ast::TypeName> : std::integral_constant<size_t, 2> {};

template <> struct tuple_element<0, yume::ast::TypeName> { using type = yume::ast::Type; };
template <> struct tuple_element<1, yume::ast::TypeName> { using type = std::string; };
} // namespace std
/// \endcond
