#pragma once

#include "qualifier.hpp"
#include "token.hpp"
#include "ty/compatibility.hpp"
#include "ty/type_base.hpp"
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

namespace llvm {
class Function;
}

namespace yume {
class Visitor;
struct Fn;
struct Ctor;
} // namespace yume

namespace yume::ast {

enum Kind {
  K_Unknown,  ///< Unknown, default, zero value. Hopefully never encountered!
  K_IfClause, ///< `IfClause`
  K_TypeName, ///< `TypeName`

  /** subclasses of Stmt */
  /**/ K_Stmt,     ///< `Stmt`
  /**/ K_Compound, ///< `Compound`
  /**/ K_While,    ///< `WhileStmt`
  /**/ K_If,       ///< `IfStmt`
  /**/ K_Return,   ///< `ReturnStmt`
  /**/ K_Program,  ///< `Program`

  /**** subclasses of Decl */
  /****/ K_Decl,       ///< `Decl`
  /****/ K_FnDecl,     ///< `FnDecl`
  /****/ K_CtorDecl,   ///< `CtorDecl`
  /****/ K_StructDecl, ///< `StructDecl`
  /****/ K_VarDecl,    ///< `VarDecl`
  /****/ K_ConstDecl,  ///< `ConstDecl`
  /****/ K_END_Decl,

  /**** subclasses of Expr */
  /****/ K_Expr,         ///< `Expr`
  /****/ K_Number,       ///< `NumberExpr`
  /****/ K_Char,         ///< `CharExpr`
  /****/ K_Bool,         ///< `BoolExpr`
  /****/ K_String,       ///< `StringExpr`
  /****/ K_Var,          ///< `VarExpr`
  /****/ K_Const,        ///< `ConstExpr`
  /****/ K_Call,         ///< `CallExpr`
  /****/ K_Ctor,         ///< `CtorExpr`
  /****/ K_Dtor,         ///< `DtorExpr`
  /****/ K_Slice,        ///< `SliceExpr`
  /****/ K_Lambda,       ///< `LambdaExpr`
  /****/ K_DirectCall,   ///< `DirectCallExpr`
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
  /**/ K_FunctionType,  ///< `TemplatedType`
  /**/ K_SelfType,      ///< `SelfType`
  /**/ K_ProxyType,     ///< `ProxyType`
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
  case K_ConstDecl: return "const decl";
  case K_StructDecl: return "struct decl";
  case K_Program: return "program";
  case K_SimpleType: return "simple type";
  case K_QualType: return "qual type";
  case K_TemplatedType: return "templated type";
  case K_FunctionType: return "function type";
  case K_SelfType: return "self type";
  case K_ProxyType: return "proxy type";
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
  case K_Const: return "const";
  case K_Lambda: return "lambda";
  case K_DirectCall: return "direct call";
  case K_Return: return "return statement";
  case K_Assign: return "assign";
  case K_FieldAccess: return "field access";
  case K_ImplicitCast: return "implicit cast";

  case K_Stmt:
  case K_Expr:
  case K_Type:
  case K_Decl:
  case K_END_Expr:
  case K_END_Decl:
  case K_END_Stmt:
  case K_END_Type: llvm_unreachable("Invalid type name for ast node");
  }
}

class TokenIterator;
class AST;
template <typename T> class AnyBase;

/// Represents "any" kind of ast node of type `T`, or the absence of one. See `OptionalExpr` and `OptionalType`.
/**
 * This template exists to avoid passing around `unique_ptr<T>`s around in code that deals with AST nodes containing
 * other generic nodes, such as `Compound`.
 * This class has the exact same layout at `AnyBase`, but is nominally explicit in its semantics, in that it may be
 * null. It replaces the earlier usages of `optional<unique_ptr<T>>` which in theory wastes memory for the "optional"
 * part when a nullptr already expresses the desired semantics.
 * Unlike `AnyBase`, this class does not perform null pointer checks, and may be default constructed, or constructed
 * from a `nullptr`.
 */
template <typename T> class OptionalAnyBase {
  unique_ptr<T> m_val;
  friend AnyBase<T>;

public:
  OptionalAnyBase() : m_val() {}
  explicit OptionalAnyBase(T* raw_ptr) : m_val{raw_ptr} {}
  template <std::convertible_to<unique_ptr<T>> U> OptionalAnyBase(U uptr) : m_val{move(uptr)} {}
  OptionalAnyBase(std::nullopt_t /* tag */) : m_val{} {}
  explicit OptionalAnyBase(optional<unique_ptr<T>> opt_uptr)
      : m_val{opt_uptr.has_value() ? move(*opt_uptr) : nullptr} {}

  [[nodiscard]] auto operator->() const -> const T* { return m_val.operator->(); }
  [[nodiscard]] auto operator*() const -> const T& { return *m_val; }
  [[nodiscard]] auto operator->() -> T* { return m_val.operator->(); }
  [[nodiscard]] auto operator*() -> T& { return *m_val; }

  [[nodiscard]] operator bool() const { return static_cast<bool>(m_val); }
  [[nodiscard]] auto has_value() const -> bool { return static_cast<bool>(m_val); }
  [[nodiscard]] auto raw_ptr() const -> const T* { return m_val.get(); }
  [[nodiscard]] auto raw_ptr() -> T* { return m_val.get(); }
};

/// Represents "any" kind of ast node of type `T`. See `AnyExpr`, `AnyStmt` and `AnyType`.
/**
 * This template exists to avoid passing around `unique_ptr<T>`s around in code that deals with AST nodes containing
 * other generic nodes, such as `Compound`.
 * This class also has strict nullptr checks when constructing, and cannot be default constructed. See
 * `OptionalAnyBase` for a similar class which may be null.
 */
template <typename T> class AnyBase : public OptionalAnyBase<T> {
  using Super = OptionalAnyBase<T>;

public:
  AnyBase() = delete;
  explicit AnyBase(T* raw_ptr) : Super{raw_ptr} { yume_assert(raw_ptr != nullptr, "AnyBase should never be null"); }
  template <std::convertible_to<unique_ptr<T>> U> AnyBase(U uptr) noexcept : Super{move(uptr)} {
    yume_assert(Super::m_val.get() != nullptr, "AnyBase should never be null");
  }
  AnyBase(OptionalAnyBase<T>&& other) : Super(move(other.m_val)) {}

  [[nodiscard]] auto operator->() const -> const T* { return Super::m_val.operator->(); }
  [[nodiscard]] auto operator*() const -> const T& { return *Super::m_val; }
  [[nodiscard]] auto operator->() -> T* { return Super::m_val.operator->(); }
  [[nodiscard]] auto operator*() -> T& { return *Super::m_val; }

  [[nodiscard]] auto raw_ptr() const -> const T* { return Super::m_val.get(); }
  [[nodiscard]] auto raw_ptr() -> T* { return Super::m_val.get(); }
};

/// Represents the relationship between multiple `AST` nodes.
/**
 * `AST` nodes can be attached to propagate type information between them.
 * TODO: describe type propagation more
 */
struct Attachment {
  /// Observers are the "children" and should have their type updated when this node's type changes
  llvm::SmallPtrSet<AST*, 2> observers{};
  /// Depends are the "parents" and should be verified for type compatibility when this node's type changes
  llvm::SmallPtrSet<AST*, 2> depends{};
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
  /// The value type of this node. Determined in the semantic phase; always empty after parsing.
  optional<ty::Type> m_val_ty{};
  /// \see Attachment
  unique_ptr<Attachment> m_attach{std::make_unique<Attachment>()};

protected:
  /// Verify the type compatibility of the depends of this node, and merge the types if possible.
  /// This is called every time the node's type is updated.
  void unify_val_ty();

  [[nodiscard]] auto tok() const noexcept -> span<Token> { return m_tok; }

  AST(Kind kind, span<Token> tok) : m_kind(kind), m_tok(tok) {}

public:
  AST(const AST&) = delete;
  AST(AST&&) = default;
  auto operator=(const AST&) -> AST& = delete;
  auto operator=(AST&&) -> AST& = delete;
  virtual ~AST() = default;

  /// Recursively visit this ast node and all its constituents. \see Visitor
  virtual void visit(Visitor& visitor) const = 0;

  [[nodiscard]] auto val_ty() const noexcept -> optional<ty::Type> { return m_val_ty; }
  [[nodiscard]] auto ensure_ty() const -> ty::Type {
    yume_assert(m_val_ty.has_value(), "Ensured that AST node has type, but one has not been assigned");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): clang-tidy doesn't accept yume_assert as an assertion
    return *m_val_ty;
  }
  void val_ty(optional<ty::Type> type) {
    m_val_ty = type;
    for (auto* i : m_attach->observers) {
      i->unify_val_ty();
    }
  }

  /// Make the type of this node depend on the type of `other`.
  /// \sa Attachment
  void attach_to(nonnull<AST*> other) {
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

/// \see AnyBase
using AnyStmt = AnyBase<Stmt>;
/// \see OptionalAnyBase
using OptionalStmt = OptionalAnyBase<Stmt>;

/// A type **annotation**. This (`ast::Type`) is distinct from the actual type of a value (`ty::Type`).
class Type : public AST {
protected:
  using AST::AST;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Type && a->kind() <= K_END_Type; }
  [[nodiscard]] auto clone() const -> Type* override = 0;
};

/// \see AnyBase
using AnyType = AnyBase<Type>;

/// \see OptionalAnyBase
using OptionalType = OptionalAnyBase<Type>;

/// Just the name of a type, always capitalized.
class SimpleType final : public Type {
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
class QualType final : public Type {
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

/// A type with explicit type parameters \e i.e. `Foo<Bar,Baz>`.
class TemplatedType final : public Type {
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
class SelfType final : public Type {
public:
  explicit SelfType(span<Token> tok) : Type(K_SelfType, tok) {}
  void visit([[maybe_unused]] Visitor& visitor) const override {}
  [[nodiscard]] auto describe() const -> string override;
  static auto classof(const AST* a) -> bool { return a->kind() == K_SelfType; }
  [[nodiscard]] auto clone() const -> SelfType* override;
};

/// A type which refers to a different type, specifically that of a struct field.
class ProxyType final : public Type {
  string m_field;

public:
  explicit ProxyType(span<Token> tok, string field) : Type(K_ProxyType, tok), m_field{move(field)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto field() const -> string { return m_field; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_ProxyType; }
  [[nodiscard]] auto clone() const -> ProxyType* override;
};

/// A function type \e i.e. `->(Foo,Bar)Baz`.
class FunctionType : public Type {
  OptionalType m_ret;
  vector<AnyType> m_args;
  bool m_fn_ptr;

public:
  FunctionType(span<Token> tok, OptionalType ret, vector<AnyType> args, bool fn_ptr)
      : Type(K_FunctionType, tok), m_ret{move(ret)}, m_args{move(args)}, m_fn_ptr(fn_ptr) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto ret() const -> const auto& { return m_ret; }
  [[nodiscard]] auto ret() -> auto& { return m_ret; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto is_fn_ptr() const { return m_fn_ptr; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_FunctionType; }
  [[nodiscard]] auto clone() const -> FunctionType* override;
};

/// A pair of a `Type` and an identifier, \e i.e. a parameter name.
struct TypeName final : public AST {
  AnyType type;
  string name;

  TypeName(span<Token> tok, AnyType type, string name) : AST(K_TypeName, tok), type{move(type)}, name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

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

/// \see AnyBase
using AnyExpr = AnyBase<Expr>;
/// \see OptionalAnyBase
using OptionalExpr = OptionalAnyBase<Expr>;

/// Number literals.
class NumberExpr final : public Expr {
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
class CharExpr final : public Expr {
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
class BoolExpr final : public Expr {
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
class StringExpr final : public Expr {
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
class VarExpr final : public Expr {
  string m_name;

public:
  explicit VarExpr(span<Token> tok, string name) : Expr(K_Var, tok), m_name(move(name)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Var; }
  [[nodiscard]] auto clone() const -> VarExpr* override;
};

/// A constant. Currently global
class ConstExpr final : public Expr {
  string m_name;
  optional<string> m_parent;

public:
  ConstExpr(span<Token> tok, string name, optional<string> parent)
      : Expr(K_Const, tok), m_name(move(name)), m_parent(move(parent)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto parent() const -> optional<string> { return m_parent; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Const; }
  [[nodiscard]] auto clone() const -> ConstExpr* override;
};

/// A function call or operator.
class CallExpr final : public Expr {
  string m_name;
  vector<AnyExpr> m_args;
  /// During semantic analysis, the `TypeWalker` performs overload selection and saves the function declaration or
  /// instantiation that this call refers to directly in the AST node, in this field.
  Fn* m_selected_overload{};

public:
  CallExpr(span<Token> tok, string name, vector<AnyExpr> args)
      : Expr(K_Call, tok), m_name{move(name)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }

  void selected_overload(Fn* fn);
  [[nodiscard]] auto selected_overload() const -> Fn*;
  static auto classof(const AST* a) -> bool { return a->kind() == K_Call; }
  [[nodiscard]] auto clone() const -> CallExpr* override;
};

/// A construction of a struct or cast of a primitive.
class CtorExpr final : public Expr {
  AnyType m_type;
  vector<AnyExpr> m_args;
  /// During semantic analysis, the `TypeWalker` performs overload selection and saves the constructor declaration that
  /// this call refers to directly in the AST node, in this field.
  Fn* m_selected_overload{};

public:
  CtorExpr(span<Token> tok, AnyType type, vector<AnyExpr> args)
      : Expr(K_Ctor, tok), m_type{move(type)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto type() const -> const auto& { return *m_type; }
  [[nodiscard]] auto type() -> auto& { return *m_type; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }

  void selected_overload(Fn* fn);
  [[nodiscard]] auto selected_overload() const -> Fn*;
  static auto classof(const AST* a) -> bool { return a->kind() == K_Ctor; }
  [[nodiscard]] auto clone() const -> CtorExpr* override;
};

/// A destruction of an object upon leaving its scope.
/**
 * This is currently marked deprecated as it is unused, however it is likely to be used again in the future, thus its
 * logic remains here to avoid redeclaring it in the future.
 */
class [[deprecated]] DtorExpr final : public Expr {
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

/// A slice literal, \e i.e. an array.
class SliceExpr final : public Expr {
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

/// A "direct" call to an anonymous function
class DirectCallExpr : public Expr {
  AnyExpr m_base;
  vector<AnyExpr> m_args;

public:
  DirectCallExpr(span<Token> tok, AnyExpr base, vector<AnyExpr> args)
      : Expr(K_DirectCall, tok), m_base{move(base)}, m_args{move(args)} {}
  void visit(Visitor& visitor) const override;
  // [[nodiscard]] auto describe() const -> string override; // TODO(rymiel)

  [[nodiscard]] auto base() const -> const auto& { return m_base; }
  [[nodiscard]] auto base() -> auto& { return m_base; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_DirectCall; }
  [[nodiscard]] auto clone() const -> DirectCallExpr* override;
};

/// An assignment (`=`).
/**
 * Assignment has a specific target (the left-hand side), and the value (the right-hand-side).
 * The target may be multiple things, such as a variable `VarExpr` or field `FieldAccessExpr`.
 * Note that some things such as indexed assignment `[]=` become a `CallExpr` instead.
 */
class AssignExpr final : public Expr {
  AnyExpr m_target;
  AnyExpr m_value;

public:
  AssignExpr(span<Token> tok, AnyExpr target, AnyExpr value)
      : Expr(K_Assign, tok), m_target{move(target)}, m_value{move(value)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto target() const -> const auto& { return m_target; }
  [[nodiscard]] auto target() -> auto& { return m_target; }
  [[nodiscard]] auto value() const -> const auto& { return m_value; }
  [[nodiscard]] auto value() -> auto& { return m_value; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Assign; }
  [[nodiscard]] auto clone() const -> AssignExpr* override;
};

/// Direct access of a field of a struct (`::`).
class FieldAccessExpr final : public Expr {
  OptionalExpr m_base;
  string m_field;
  int m_offset = -1;

public:
  FieldAccessExpr(span<Token> tok, OptionalExpr base, string field)
      : Expr(K_FieldAccess, tok), m_base{move(base)}, m_field{move(field)} {}
  void visit(Visitor& visitor) const override;

  [[nodiscard]] auto base() const -> const auto& { return m_base; }
  [[nodiscard]] auto base() -> auto& { return m_base; }
  [[nodiscard]] auto field() const -> string { return m_field; }
  void offset(int offset) { m_offset = offset; }
  [[nodiscard]] auto offset() const -> int { return m_offset; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FieldAccess; }
  [[nodiscard]] auto clone() const -> FieldAccessExpr* override;
};

/// Represents an implicit cast to a different type, performed during semantic analysis
/**
 * Note that implicit casts have no direct "textual" representation in actual source code, they are only materialized
 * during semantic analysis.
 */
class ImplicitCastExpr final : public Expr {
  AnyExpr m_base;
  /// The conversion steps performed during this cast.
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

/// A statement consisting of multiple other statements, \e i.e. the body of a function.
class Compound final : public Stmt {
  vector<AnyStmt> m_body;

public:
  void visit(Visitor& visitor) const override;
  explicit Compound(span<Token> tok, vector<AnyStmt> body) : Stmt(K_Compound, tok), m_body{move(body)} {}

  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_Compound; }
  [[nodiscard]] auto clone() const -> Compound* override;
};

/// A local definition of an anonymous function
class LambdaExpr final : public Expr {
  vector<TypeName> m_args;
  OptionalType m_ret;
  Compound m_body;
  std::set<string> m_annotations;
  vector<string> m_closured_names{};
  vector<AST*> m_closured_nodes{};
  llvm::Function* m_llvm_fn{};

public:
  LambdaExpr(span<Token> tok, vector<TypeName> args, OptionalType ret, Compound body, std::set<string> annotations)
      : Expr(K_Lambda, tok), m_args(move(args)), m_ret(move(ret)), m_body(move(body)),
        m_annotations(move(annotations)) {}
  void visit(Visitor& visitor) const override;
  // [[nodiscard]] auto describe() const -> string override; // TODO(rymiel)

  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto ret() const -> const auto& { return m_ret; }
  [[nodiscard]] auto ret() -> auto& { return m_ret; }
  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  [[nodiscard]] auto annotations() const -> const auto& { return m_annotations; }
  [[nodiscard]] auto annotations() -> auto& { return m_annotations; }
  [[nodiscard]] auto closured_names() const -> const auto& { return m_closured_names; }
  [[nodiscard]] auto closured_names() -> auto& { return m_closured_names; }
  [[nodiscard]] auto closured_nodes() const -> const auto& { return m_closured_nodes; }
  [[nodiscard]] auto closured_nodes() -> auto& { return m_closured_nodes; }
  void llvm_fn(llvm::Function* fn);
  [[nodiscard]] auto llvm_fn() const -> llvm::Function*;

  static auto classof(const AST* a) -> bool { return a->kind() == K_Lambda; }
  [[nodiscard]] auto clone() const -> LambdaExpr* override;
};

/// Base class for a named declaration.
class Decl : public Stmt {
protected:
  using Stmt::Stmt;

public:
  static auto classof(const AST* a) -> bool { return a->kind() >= K_Decl && a->kind() <= K_END_Decl; }
  [[nodiscard]] auto clone() const -> Decl* override = 0;

  [[nodiscard]] virtual auto name() const -> string = 0;
};

/// A declaration of a function (`def`).
class FnDecl final : public Decl {
public:
  using extern_decl_t = struct {
    string name;
    bool varargs;
  };

  using body_t = variant<Compound, string, extern_decl_t>;

  static constexpr auto ANN_EXTERN = "extern";

private:
  string m_name;
  std::set<string> m_annotations;
  vector<TypeName> m_args;
  vector<string> m_type_args;
  OptionalType m_ret;
  /// If this function declaration refers to a primitive, this field is a string representing the name of the primitive.
  /// If it's an external method, this field is a pair of the extern name and whether the method is varargs.
  /// Otherise, this function declaration refers to a regular function and this field holds the body of that function.
  body_t m_body;

public:
  FnDecl(span<Token> tok, string name, vector<TypeName> args, vector<string> type_args, OptionalType ret, body_t body,
         std::set<string> annotations)
      : Decl(K_FnDecl, tok), m_name{move(name)}, m_annotations(move(annotations)), m_args{move(args)},
        m_type_args{move(type_args)}, m_ret{move(ret)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string override { return m_name; }
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto type_args() const { return m_type_args; }
  [[nodiscard]] auto ret() const -> const auto& { return m_ret; }
  [[nodiscard]] auto ret() -> auto& { return m_ret; }
  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  [[nodiscard]] auto varargs() const -> bool { return extern_decl() && std::get<extern_decl_t>(m_body).varargs; }
  [[nodiscard]] auto primitive() const -> bool { return holds_alternative<string>(m_body); }
  [[nodiscard]] auto extern_decl() const -> bool { return holds_alternative<extern_decl_t>(m_body); }
  [[nodiscard]] auto annotations() const -> const auto& { return m_annotations; }
  [[nodiscard]] auto annotations() -> auto& { return m_annotations; }
  [[nodiscard]] auto extern_linkage() const -> bool { return extern_decl() || m_annotations.contains(ANN_EXTERN); }
  void make_extern_linkage(bool value = true) {
    if (value)
      m_annotations.emplace(ANN_EXTERN);
    else
      m_annotations.erase(ANN_EXTERN);
  }
  static auto classof(const AST* a) -> bool { return a->kind() == K_FnDecl; }
  [[nodiscard]] auto clone() const -> FnDecl* override;
};

/// A declaration of a custom constructor (`def :new`).
/**
 * The arguments of a constructor may be regular `TypeName`s, just as in regular function declarations, but they also
 * may be a `FieldAccessExpr` without a base (such as `::x`), representing a shorthand where the field is set
 * immediately, and the type is deduced to be that of the field.
 * Essentially the following two declarations are identical:
 * \code
 * struct Foo(data I32)
 *   def :new(::data)
 *   end
 *
 *   def :new(data I32)
 *     ::data = data
 *   end
 * end
 * \endcode
 */
class CtorDecl final : public Decl {
private:
  vector<TypeName> m_args;
  Compound m_body;

public:
  CtorDecl(span<Token> tok, vector<TypeName> args, Compound body)
      : Decl(K_CtorDecl, tok), m_args{move(args)}, m_body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string override { return ":new"; } // TODO(rymiel): Magic value?
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto body() const -> const auto& { return m_body; }
  [[nodiscard]] auto body() -> auto& { return m_body; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_CtorDecl; }
  [[nodiscard]] auto clone() const -> CtorDecl* override;
};

/// A declaration of a struct (`struct`).
class StructDecl final : public Decl {
  string m_name;
  vector<TypeName> m_fields;
  vector<string> m_type_args;
  Compound m_body;

public:
  StructDecl(span<Token> tok, string name, vector<TypeName> fields, vector<string> type_args, Compound body)
      : Decl(K_StructDecl, tok), m_name{move(name)}, m_fields{move(fields)}, m_type_args{move(type_args)},
        m_body(move(body)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string override { return m_name; }
  [[nodiscard]] constexpr auto fields() const -> const auto& { return m_fields; }
  [[nodiscard]] constexpr auto fields() -> auto& { return m_fields; }
  [[nodiscard]] constexpr auto body() const -> const auto& { return m_body; }
  [[nodiscard]] constexpr auto body() -> auto& { return m_body; }
  [[nodiscard]] auto type_args() const { return m_type_args; }
  static auto classof(const AST* a) -> bool { return a->kind() == K_StructDecl; }
  [[nodiscard]] auto clone() const -> StructDecl* override;
};

/// A declaration of a local variable (`let`).
class VarDecl final : public Decl {
  string m_name;
  OptionalType m_type;
  AnyExpr m_init;

public:
  VarDecl(span<Token> tok, string name, OptionalType type, AnyExpr init)
      : Decl(K_VarDecl, tok), m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string override { return m_name; }
  [[nodiscard]] auto type() const -> const auto& { return m_type; }
  [[nodiscard]] auto type() -> auto& { return m_type; }
  [[nodiscard]] auto init() const -> const auto& { return m_init; }
  [[nodiscard]] auto init() -> auto& { return m_init; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_VarDecl; }
  [[nodiscard]] auto clone() const -> VarDecl* override;
};

/// A declaration of a constant (`const`).
class ConstDecl final : public Decl {
  string m_name;
  AnyType m_type; // TODO(rymiel): make optional?
  AnyExpr m_init;

public:
  ConstDecl(span<Token> tok, string name, AnyType type, AnyExpr init)
      : Decl(K_ConstDecl, tok), m_name{move(name)}, m_type{move(type)}, m_init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  [[nodiscard]] auto name() const -> string override { return m_name; }
  [[nodiscard]] auto type() const -> const auto& { return m_type; }
  [[nodiscard]] auto type() -> auto& { return m_type; }
  [[nodiscard]] auto init() const -> const auto& { return m_init; }
  [[nodiscard]] auto init() -> auto& { return m_init; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_ConstDecl; }
  [[nodiscard]] auto clone() const -> ConstDecl* override;
};

/// A while loop (`while`).
class WhileStmt final : public Stmt {
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
struct IfClause final : public AST {
public:
  AnyExpr cond;
  Compound body;

  IfClause(span<Token> tok, AnyExpr cond, Compound body)
      : AST(K_IfClause, tok), cond{move(cond)}, body{move(body)} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_IfClause; }
  [[nodiscard]] auto clone() const -> IfClause* override;
};

/// An if statement (`if`), with one or more `IfClause`s, and optionally an else clause.
struct IfStmt final : public Stmt {
public:
  vector<IfClause> clauses;
  optional<Compound> else_clause;

  IfStmt(span<Token> tok, vector<IfClause> clauses, optional<Compound> else_clause)
      : Stmt(K_If, tok), clauses{move(clauses)}, else_clause{move(else_clause)} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_If; }
  [[nodiscard]] auto clone() const -> IfStmt* override;
};

/// Return from a function body.
struct ReturnStmt final : public Stmt {
public:
  OptionalExpr expr;
  VarDecl* extends_lifetime{};

  explicit ReturnStmt(span<Token> tok, OptionalExpr expr) : Stmt(K_Return, tok), expr{move(expr)} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_Return; }
  [[nodiscard]] auto clone() const -> ReturnStmt* override;
};

/// The top level structure of a file of source code.
struct Program final : public Stmt {
public:
  vector<AnyStmt> body;

  explicit Program(span<Token> tok, vector<AnyStmt> body) : Stmt(K_Program, tok), body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] static auto parse(TokenIterator& tokens) -> unique_ptr<Program>;

  static auto classof(const AST* a) -> bool { return a->kind() == K_Program; }
  [[nodiscard]] auto clone() const -> Program* override;
};
} // namespace yume::ast
