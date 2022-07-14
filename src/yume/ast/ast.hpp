#pragma once

#include "qualifier.hpp"
#include "token.hpp"
#include "ty/compatibility.hpp"
#include "util.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/ErrorHandling.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace yume {
class Visitor;
struct Fn;
struct Ctor;
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
  /**/ K_CtorDecl,   ///< `CtorDecl`
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
  case K_CtorDecl: return "ctor decl";
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

  case K_Stmt:
  case K_Expr:
  case K_Type:
  case K_END_Expr:
  case K_END_Stmt:
  case K_END_Type: llvm_unreachable("Invalid type name for ast node");
  }
}

class TokenIterator;

class AST;

template <typename T> struct AnyBase {
  unique_ptr<T> val;

  explicit AnyBase(T* raw_ptr) : val{raw_ptr} {}
  AnyBase(unique_ptr<T> uptr) : val{move(uptr)} {}
  template <typename U>
  requires std::is_base_of_v<T, U> AnyBase(unique_ptr<U> uptr) : val{move(uptr)} {}

  [[nodiscard]] auto operator->() const -> const T* { return val.operator->(); }
  [[nodiscard]] auto operator*() const -> const T& { return *val; }
  [[nodiscard]] auto operator->() -> T* { return val.operator->(); }
  [[nodiscard]] auto operator*() -> T& { return *val; }
};

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
  [[nodiscard]] auto location() const -> Loc;

  /// A short, string representation for debugging.
  [[nodiscard]] virtual auto describe() const -> string;

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

using AnyStmt = AnyBase<Stmt>;

/// A type **annotation**. This (`ast::Type`) is distinct from the actual type of a value (`ty::Type`).
class Type : public AST {
protected:
  using AST::AST;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Type && a->kind() <= K_END_Type; }
  [[nodiscard]] auto clone() const -> Type* override = 0;
};

using AnyType = AnyBase<Type>;

/// Just the name of a type, always capitalized.
class SimpleType : public Type {
  string m_name;

public:
  explicit SimpleType(span<Token> tok, string name) : Type(K_SimpleType, tok), m_name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_SimpleType; }
  [[nodiscard]] auto clone() const -> SimpleType* override;
};

/// A type with a `Qualifier` like `mut` or `[]` following.
class QualType : public Type {
  AnyType m_base;
  Qualifier m_qualifier;

public:
  QualType(span<Token> tok, AnyType base, Qualifier qualifier)
      : Type(K_QualType, tok), m_base{move(base)}, m_qualifier{qualifier} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] constexpr auto qualifier() const -> Qualifier { return m_qualifier; }
  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_QualType; }
  [[nodiscard]] auto clone() const -> QualType* override;
};

/// A type with explicit type parameters \e i.e. Foo<Bar,Baz>.
class TemplatedType : public Type {
  AnyType m_base;
  vector<AnyType> m_type_args;

public:
  TemplatedType(span<Token> tok, AnyType base, vector<AnyType> type_args)
      : Type(K_TemplatedType, tok), m_base{move(base)}, m_type_args{move(type_args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto type_vars() const -> const auto& { return m_type_args; }
  [[nodiscard]] auto type_vars() -> auto& { return m_type_args; }
  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_TemplatedType; }
  [[nodiscard]] auto clone() const -> TemplatedType* override;
};

/// The `self` type.
class SelfType : public Type {
public:
  explicit SelfType(span<Token> tok) : Type(K_SelfType, tok) {}
  void visit([[maybe_unused]] Visitor& visitor) const override {}
  [[nodiscard]] auto describe() const -> string override;
  static auto classof(const AST* a) -> bool { return a->kind() == K_SelfType; }
  [[nodiscard]] auto clone() const -> SelfType* override;
};

/// A pair of a `Type` and an identifier, \e i.e. a parameter name.
class TypeName : public AST {
  AnyType m_type;
  string m_name;

public:
  TypeName(span<Token> tok, AnyType type, string name)
      : AST(K_TypeName, tok), m_type{move(type)}, m_name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto type() const -> const auto& { return *m_type; }
  [[nodiscard]] auto type() -> auto& { return *m_type; }
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

using AnyExpr = AnyBase<Expr>;

/// Number literals.
class NumberExpr : public Expr {
  int64_t m_val;

public:
  explicit NumberExpr(span<Token> tok, int64_t val) : Expr(K_Number, tok), m_val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

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
  [[nodiscard]] auto describe() const -> string override;

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
  [[nodiscard]] auto describe() const -> string override;

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
  [[nodiscard]] auto describe() const -> string override;

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
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Var; }
  [[nodiscard]] auto clone() const -> VarExpr* override;
};

/// A function call or operator.
class CallExpr : public Expr {
  string m_name;
  vector<AnyExpr> m_args;
  mutable Fn* m_selected_overload{};

public:
  CallExpr(span<Token> tok, string name, vector<AnyExpr> args)
      : Expr(K_Call, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto direct_args() -> auto& { return m_args; }

  void selected_overload(Fn* fn) const;
  [[nodiscard]] auto selected_overload() const -> Fn*;
  static auto classof(const AST* a) -> bool { return a->kind() == K_Call; }
  [[nodiscard]] auto clone() const -> CallExpr* override;
};

/// A construction of a struct or cast of a primitive.
class CtorExpr : public Expr {
  AnyType m_type;
  string m_name;
  vector<AnyExpr> m_args;
  mutable Ctor* m_selected_overload{};

public:
  CtorExpr(span<Token> tok, AnyType type, string name, vector<AnyExpr> args)
      : Expr(K_Ctor, tok), m_type{move(type)}, m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto type() const -> const auto& { return *m_type; }
  [[nodiscard]] auto type() -> auto& { return *m_type; }
  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }

  void selected_overload(Ctor* fn) const;
  [[nodiscard]] auto selected_overload() const -> Ctor*;
  static auto classof(const AST* a) -> bool { return a->kind() == K_Ctor; }
  [[nodiscard]] auto clone() const -> CtorExpr* override;
};

/// A destruction of an object upon leaving its scope.
class [[deprecated]] DtorExpr : public Expr {
  AnyExpr m_base;

public:
  DtorExpr(span<Token> tok, AnyExpr base) : Expr(K_Dtor, tok), m_base{move(base)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto base() const -> const auto& { return *m_base; }
  [[nodiscard]] auto base() -> auto& { return *m_base; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Dtor; }
  [[nodiscard]] auto clone() const -> DtorExpr* override;
};

/// A slice literal, i.e. an array.
class SliceExpr : public Expr {
  AnyType m_type;
  vector<AnyExpr> m_args;

public:
  SliceExpr(span<Token> tok, AnyType type, vector<AnyExpr> args)
      : Expr(K_Slice, tok), m_type{move(type)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto type() const -> const auto& { return *m_type; }
  [[nodiscard]] auto type() -> auto& { return *m_type; }
  [[nodiscard]] constexpr auto args() const -> const auto& { return m_args; }
  [[nodiscard]] constexpr auto args() -> auto& { return m_args; }
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
  AnyExpr m_target;
  AnyExpr m_value;

public:
  AssignExpr(span<Token> tok, AnyExpr target, AnyExpr value)
      : Expr(K_Assign, tok), m_target{move(target)}, m_value{move(value)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto target() const -> const auto& { return *m_target; }
  [[nodiscard]] auto target() -> auto& { return *m_target; }
  [[nodiscard]] auto value() const -> const auto& { return *m_value; }
  [[nodiscard]] auto value() -> auto& { return *m_value; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Assign; }
  [[nodiscard]] auto clone() const -> AssignExpr* override;
};

/// Direct access of a field of a struct (`::`).
class FieldAccessExpr : public Expr {
  optional<AnyExpr> m_base;
  string m_field;
  mutable int m_offset = -1;

public:
  FieldAccessExpr(span<Token> tok, optional<AnyExpr> base, string field)
      : Expr(K_FieldAccess, tok), m_base{move(base)}, m_field{move(field)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto base() const -> const auto& { return m_base; }
  [[nodiscard]] auto base() -> auto& { return m_base; }
  [[nodiscard]] auto field() const -> string { return m_field; }
  void offset(int offset) const { m_offset = offset; }
  [[nodiscard]] auto offset() const -> int { return m_offset; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FieldAccess; }
  [[nodiscard]] auto clone() const -> FieldAccessExpr* override;
};

class ImplicitCastExpr : public Expr {
  AnyExpr m_base;
  ty::Conv m_conversion;

public:
  ImplicitCastExpr(span<Token> tok, AnyExpr base, ty::Conv conversion)
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
  vector<AnyStmt> m_body;

public:
  void visit(Visitor& visitor) const override;
  explicit Compound(span<Token> tok, vector<AnyStmt> body) : Stmt(K_Compound, tok), m_body{move(body)} {}

  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Compound; }
  [[nodiscard]] auto clone() const -> Compound* override;
};

/// A declaration of a function (`def`).
class FnDecl : public Stmt {
public:
  using arg_t = TypeName;

private:
  string m_name;
  bool m_varargs{};
  vector<arg_t> m_args;
  vector<string> m_type_args;
  optional<AnyType> m_ret;
  variant<Compound, string> m_body;

public:
  FnDecl(span<Token> tok, string name, vector<arg_t> args, vector<string> type_args, optional<AnyType> ret,
         Compound body)
      : Stmt(K_FnDecl, tok), m_name{move(name)}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(body)} {}
  FnDecl(span<Token> tok, string name, vector<arg_t> args, vector<string> type_args, optional<AnyType> ret,
         bool varargs, string primitive)
      : Stmt(K_FnDecl, tok), m_name{move(name)}, m_varargs{varargs}, m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(primitive)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto type_args() const { return m_type_args; }
  [[nodiscard]] constexpr auto ret() const -> const auto& { return m_ret; }
  [[nodiscard]] constexpr auto ret() -> auto& { return m_ret; }
  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto body() -> auto& { return m_body; }
  [[nodiscard]] constexpr auto varargs() const -> bool { return m_varargs; }
  [[nodiscard]] constexpr auto primitive() const -> bool { return holds_alternative<string>(m_body); }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FnDecl; }
  [[nodiscard]] auto clone() const -> FnDecl* override;
};

/// A declaration of a custom constructor (`def :`).
class CtorDecl : public Stmt {
public:
  using arg_t = variant<TypeName, FieldAccessExpr>;

private:
  string m_name;
  vector<arg_t> m_args;
  Compound m_body;

public:
  CtorDecl(span<Token> tok, string name, vector<arg_t> args, Compound body)
      : Stmt(K_CtorDecl, tok), m_name{move(name)}, m_args{move(args)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_CtorDecl; }
  [[nodiscard]] auto clone() const -> CtorDecl* override;
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
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] constexpr auto fields() const -> const auto& { return m_fields; }
  [[nodiscard]] constexpr auto fields() -> auto& { return m_fields; }
  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto body() -> auto& { return m_body; }
  [[nodiscard]] auto type_args() const { return m_type_args; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_StructDecl; }
  [[nodiscard]] auto clone() const -> StructDecl* override;
};

/// A declaration of a local variable (`let`).
class VarDecl : public Stmt {
  string m_name;
  optional<AnyType> m_type;
  AnyExpr m_init;

public:
  VarDecl(span<Token> tok, string name, optional<AnyType> type, AnyExpr init)
      : Stmt(K_VarDecl, tok), m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto type() const -> const auto& { return m_type; }
  [[nodiscard]] auto type() -> auto& { return m_type; }
  [[nodiscard]] auto init() const -> const auto& { return *m_init; }
  [[nodiscard]] auto init() -> auto& { return *m_init; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_VarDecl; }
  [[nodiscard]] auto clone() const -> VarDecl* override;
};

/// A while loop (`while`).
class WhileStmt : public Stmt {
  AnyExpr m_cond;
  Compound m_body;

public:
  WhileStmt(span<Token> tok, AnyExpr cond, Compound body)
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
  AnyExpr m_cond;
  Compound m_body;

public:
  IfClause(span<Token> tok, AnyExpr cond, Compound body)
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
  optional<AnyExpr> m_expr;

public:
  explicit ReturnStmt(span<Token> tok, optional<AnyExpr> expr) : Stmt(K_Return, tok), m_expr{move(expr)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto expr() const -> const auto& { return m_expr; }
  [[nodiscard]] auto expr() -> auto& { return m_expr; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Return; }
  [[nodiscard]] auto clone() const -> ReturnStmt* override;
};

/// The top level structure of a file of source code.
class Program : public Stmt {
  vector<AnyStmt> m_body;

public:
  explicit Program(span<Token> tok, vector<AnyStmt> body) : Stmt(K_Program, tok), m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;

  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto body() -> auto& { return m_body; }
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
