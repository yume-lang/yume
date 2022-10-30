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
  /****/ K_BinaryLogic,  ///< `BinaryLogicExpr`
  /****/ K_Ctor,         ///< `CtorExpr`
  /****/ K_Dtor,         ///< `DtorExpr`
  /****/ K_Slice,        ///< `SliceExpr`
  /****/ K_Lambda,       ///< `LambdaExpr`
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
  case K_BinaryLogic: return "binary logic";
  case K_Ctor: return "constructor";
  case K_Dtor: return "destructor";
  case K_Slice: return "slice";
  case K_Var: return "var";
  case K_Const: return "const";
  case K_Lambda: return "lambda";
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
struct SimpleType final : public Type {
public:
  string name;

  explicit SimpleType(span<Token> tok, string name) : Type(K_SimpleType, tok), name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_SimpleType; }
  [[nodiscard]] auto clone() const -> SimpleType* override;
};

/// A type with a `Qualifier` like `mut` or `[]` following.
struct QualType final : public Type {
public:
  AnyType base;
  Qualifier qualifier;

  QualType(span<Token> tok, AnyType base, Qualifier qualifier)
      : Type(K_QualType, tok), base{move(base)}, qualifier{qualifier} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_QualType; }
  [[nodiscard]] auto clone() const -> QualType* override;
};

/// A type with explicit type parameters \e i.e. `Foo<Bar,Baz>`.
struct TemplatedType final : public Type {
public:
  AnyType base;
  vector<AnyType> type_args;

  TemplatedType(span<Token> tok, AnyType base, vector<AnyType> type_args)
      : Type(K_TemplatedType, tok), base{move(base)}, type_args{move(type_args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_TemplatedType; }
  [[nodiscard]] auto clone() const -> TemplatedType* override;
};

/// The `self` type.
struct SelfType final : public Type {
public:
  explicit SelfType(span<Token> tok) : Type(K_SelfType, tok) {}
  void visit([[maybe_unused]] Visitor& visitor) const override {}
  [[nodiscard]] auto describe() const -> string override { return "self"; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_SelfType; }
  [[nodiscard]] auto clone() const -> SelfType* override;
};

/// A type which refers to a different type, specifically that of a struct field.
struct ProxyType final : public Type {
public:
  string field;

  explicit ProxyType(span<Token> tok, string field) : Type(K_ProxyType, tok), field{move(field)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return field; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_ProxyType; }
  [[nodiscard]] auto clone() const -> ProxyType* override;
};

/// A function type \e i.e. `->(Foo,Bar)Baz`.
struct FunctionType : public Type {
public:
  OptionalType ret;
  vector<AnyType> args;
  bool fn_ptr;

  FunctionType(span<Token> tok, OptionalType ret, vector<AnyType> args, bool fn_ptr)
      : Type(K_FunctionType, tok), ret{move(ret)}, args{move(args)}, fn_ptr(fn_ptr) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_FunctionType; }
  [[nodiscard]] auto clone() const -> FunctionType* override;
};

/// A pair of a `Type` and an identifier, \e i.e. a parameter name.
struct TypeName final : public AST {
  AnyType type;
  string name;

  TypeName(span<Token> tok, AnyType type, string name) : AST(K_TypeName, tok), type{move(type)}, name{move(name)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return name; }

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
struct NumberExpr final : public Expr {
public:
  int64_t val;

  explicit NumberExpr(span<Token> tok, int64_t val) : Expr(K_Number, tok), val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return std::to_string(val); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Number; }
  [[nodiscard]] auto clone() const -> NumberExpr* override;
};

/// Char literals.
struct CharExpr final : public Expr {
public:
  uint8_t val;

  explicit CharExpr(span<Token> tok, uint8_t val) : Expr(K_Char, tok), val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return std::to_string(val); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Char; }
  [[nodiscard]] auto clone() const -> CharExpr* override;
};

/// Bool literals (`true` or `false`).
struct BoolExpr final : public Expr {
public:
  bool val;

  explicit BoolExpr(span<Token> tok, bool val) : Expr(K_Bool, tok), val(val) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return val ? "true" : "false"; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Bool; }
  [[nodiscard]] auto clone() const -> BoolExpr* override;
};

/// String literals.
struct StringExpr final : public Expr {
public:
  string val;

  explicit StringExpr(span<Token> tok, string val) : Expr(K_String, tok), val(move(val)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return val; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_String; }
  [[nodiscard]] auto clone() const -> StringExpr* override;
};

/// A variable, \e i.e. just an identifier.
struct VarExpr final : public Expr {
public:
  string name;

  explicit VarExpr(span<Token> tok, string name) : Expr(K_Var, tok), name(move(name)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Var; }
  [[nodiscard]] auto clone() const -> VarExpr* override;
};

/// A constant. Currently global
struct ConstExpr final : public Expr {
public:
  string name;
  optional<string> parent;

  ConstExpr(span<Token> tok, string name, optional<string> parent)
      : Expr(K_Const, tok), name(move(name)), parent(move(parent)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Const; }
  [[nodiscard]] auto clone() const -> ConstExpr* override;
};

/// A function call or operator.
struct CallExpr final : public Expr {
public:
  string name;
  OptionalType receiver;
  vector<AnyExpr> args;
  /// During semantic analysis, the `TypeWalker` performs overload selection and saves the function declaration or
  /// instantiation that this call refers to directly in the AST node, in this field.
  Fn* selected_overload{};

  CallExpr(span<Token> tok, string name, OptionalType receiver, vector<AnyExpr> args)
      : Expr(K_Call, tok), name{move(name)}, receiver{move(receiver)}, args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Call; }
  [[nodiscard]] auto clone() const -> CallExpr* override;
};

/// A logical operator such as `||` or `&&`. Since these aren't overloadable, they have their own AST node.
struct BinaryLogicExpr final : public Expr {
public:
  Atom operation;
  AnyExpr lhs;
  AnyExpr rhs;

  BinaryLogicExpr(span<Token> tok, Atom operation, AnyExpr lhs, AnyExpr rhs)
      : Expr(K_BinaryLogic, tok), operation{operation}, lhs{move(lhs)}, rhs{move(rhs)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return static_cast<string>(operation); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_BinaryLogic; }
  [[nodiscard]] auto clone() const -> BinaryLogicExpr* override;
};

/// A construction of a struct or cast of a primitive.
struct CtorExpr final : public Expr {
public:
  AnyType type;
  vector<AnyExpr> args;
  /// During semantic analysis, the `TypeWalker` performs overload selection and saves the constructor declaration that
  /// this call refers to directly in the AST node, in this field.
  Fn* selected_overload{};

  CtorExpr(span<Token> tok, AnyType type, vector<AnyExpr> args)
      : Expr(K_Ctor, tok), type{move(type)}, args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return type->describe(); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Ctor; }
  [[nodiscard]] auto clone() const -> CtorExpr* override;
};

/// A destruction of an object upon leaving its scope.
/**
 * This is currently marked deprecated as it is unused, however it is likely to be used again in the future, thus its
 * logic remains here to avoid redeclaring it in the future.
 */
struct [[deprecated]] DtorExpr final : public Expr {
public:
  AnyExpr base;

  DtorExpr(span<Token> tok, AnyExpr base) : Expr(K_Dtor, tok), base{move(base)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return base->describe(); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Dtor; }
  [[nodiscard]] auto clone() const -> DtorExpr* override;
};

/// A slice literal, \e i.e. an array.
struct SliceExpr final : public Expr {
public:
  AnyType type;
  vector<AnyExpr> args;

  SliceExpr(span<Token> tok, AnyType type, vector<AnyExpr> args)
      : Expr(K_Slice, tok), type{move(type)}, args{move(args)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto describe() const -> string override { return type->describe(); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Slice; }
  [[nodiscard]] auto clone() const -> SliceExpr* override;
};

/// An assignment (`=`).
/**
 * Assignment has a specific target (the left-hand side), and the value (the right-hand-side).
 * The target may be multiple things, such as a variable `VarExpr` or field `FieldAccessExpr`.
 * Note that some things such as indexed assignment `[]=` become a `CallExpr` instead.
 */
struct AssignExpr final : public Expr {
public:
  AnyExpr target;
  AnyExpr value;

  AssignExpr(span<Token> tok, AnyExpr target, AnyExpr value)
      : Expr(K_Assign, tok), target{move(target)}, value{move(value)} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_Assign; }
  [[nodiscard]] auto clone() const -> AssignExpr* override;
};

/// Direct access of a field of a struct (`::`).
struct FieldAccessExpr final : public Expr {
public:
  OptionalExpr base;
  string field;
  int offset = -1;

  FieldAccessExpr(span<Token> tok, OptionalExpr base, string field)
      : Expr(K_FieldAccess, tok), base{move(base)}, field{move(field)} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_FieldAccess; }
  [[nodiscard]] auto clone() const -> FieldAccessExpr* override;
};

/// Represents an implicit cast to a different type, performed during semantic analysis
/**
 * Note that implicit casts have no direct "textual" representation in actual source code, they are only materialized
 * during semantic analysis.
 */
struct ImplicitCastExpr final : public Expr {
public:
  AnyExpr base;
  /// The conversion steps performed during this cast.
  ty::Conv conversion;

  ImplicitCastExpr(span<Token> tok, AnyExpr base, ty::Conv conversion)
      : Expr(K_ImplicitCast, tok), base{move(base)}, conversion{conversion} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_ImplicitCast; }
  [[nodiscard]] auto clone() const -> ImplicitCastExpr* override;
};

/// A statement consisting of multiple other statements, \e i.e. the body of a function.
struct Compound final : public Stmt {
public:
  vector<AnyStmt> body;

  void visit(Visitor& visitor) const override;
  explicit Compound(span<Token> tok, vector<AnyStmt> body) : Stmt(K_Compound, tok), body{move(body)} {}

  [[nodiscard]] auto begin() { return body.begin(); }
  [[nodiscard]] auto end() { return body.end(); }
  [[nodiscard]] auto begin() const { return body.begin(); }
  [[nodiscard]] auto end() const { return body.end(); }

  static auto classof(const AST* a) -> bool { return a->kind() == K_Compound; }
  [[nodiscard]] auto clone() const -> Compound* override;
};

/// A local definition of an anonymous function
struct LambdaExpr final : public Expr {
public:
  vector<TypeName> args;
  OptionalType ret;
  Compound body;
  std::set<string> annotations;
  vector<string> closured_names{};
  vector<AST*> closured_nodes{};
  llvm::Function* llvm_fn{};

  LambdaExpr(span<Token> tok, vector<TypeName> args, OptionalType ret, Compound body, std::set<string> annotations)
      : Expr(K_Lambda, tok), args(move(args)), ret(move(ret)), body(move(body)), annotations(move(annotations)) {}
  void visit(Visitor& visitor) const override;
  // [[nodiscard]] auto describe() const -> string override; // TODO(rymiel)

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

  [[nodiscard]] virtual auto decl_name() const -> string = 0;
  [[nodiscard]] auto describe() const -> string final { return decl_name(); }
};

/// A declaration of a function (`def`).
struct FnDecl final : public Decl {
public:
  using extern_decl_t = struct {
    string name;
    bool varargs;
  };
  using abstract_decl_t = struct {};

  using body_base_t = visitable_variant<Compound, string, extern_decl_t, abstract_decl_t>;
  struct Body : public body_base_t {
    using body_base_t::body_base_t;
  };

  static constexpr auto ANN_EXTERN = "extern";
  static constexpr auto ANN_OVERRIDE = "override";

  string name;
  std::set<string> annotations;
  vector<TypeName> args;
  vector<string> type_args;
  OptionalType ret;
  /// If this function declaration refers to a primitive, this field is a string representing the name of the primitive.
  /// If it's an external method, this field is a pair of the extern name and whether the method is varargs.
  /// Otherise, this function declaration refers to a regular function and this field holds the body of that function.
  Body body;
  /// During semantic analysis, AST nodes are converted to a format better suited for semantic analysis. This field
  /// will be set once said conversion is performed.
  Fn* sema_decl{};

  FnDecl(span<Token> tok, string name, vector<TypeName> args, vector<string> type_args, OptionalType ret, Body body,
         std::set<string> annotations)
      : Decl(K_FnDecl, tok), name{move(name)},
        annotations(move(annotations)), args{move(args)}, type_args{move(type_args)}, ret{move(ret)}, body{move(body)} {
  }
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto decl_name() const -> string override { return name; }

  [[nodiscard]] auto varargs() const -> bool { return extern_decl() && std::get<extern_decl_t>(body).varargs; }
  [[nodiscard]] auto primitive() const -> bool { return holds_alternative<string>(body); }
  [[nodiscard]] auto extern_decl() const -> bool { return holds_alternative<extern_decl_t>(body); }
  [[nodiscard]] auto abstract() const -> bool { return holds_alternative<abstract_decl_t>(body); }
  [[nodiscard]] auto extern_linkage() const -> bool { return extern_decl() || annotations.contains(ANN_EXTERN); }
  [[nodiscard]] auto override() const -> bool { return annotations.contains(ANN_OVERRIDE); }
  void make_extern_linkage(bool value = true) {
    if (value)
      annotations.emplace(ANN_EXTERN);
    else
      annotations.erase(ANN_EXTERN);
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
struct CtorDecl final : public Decl {
public:
  vector<TypeName> args;
  Compound body;

  CtorDecl(span<Token> tok, vector<TypeName> args, Compound body)
      : Decl(K_CtorDecl, tok), args{move(args)}, body{move(body)} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto decl_name() const -> string override { return ":new"; } // TODO(rymiel): Magic value?

  static auto classof(const AST* a) -> bool { return a->kind() == K_CtorDecl; }
  [[nodiscard]] auto clone() const -> CtorDecl* override;
};

/// A declaration of a struct (`struct`) or an interface (`interface`).
struct StructDecl final : public Decl {
public:
  string name;
  vector<TypeName> fields;
  vector<string> type_args;
  Compound body;
  OptionalType implements;
  bool is_interface;

  StructDecl(span<Token> tok, string name, vector<TypeName> fields, vector<string> type_args, Compound body,
             OptionalType implements, bool is_interface = false)
      : Decl(K_StructDecl, tok), name{move(name)}, fields{move(fields)}, type_args{move(type_args)}, body{move(body)},
        implements{move(implements)}, is_interface{is_interface} {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto decl_name() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_StructDecl; }
  [[nodiscard]] auto clone() const -> StructDecl* override;
};

/// A declaration of a local variable (`let`).
struct VarDecl final : public Decl {
public:
  string name;
  OptionalType type;
  AnyExpr init;

  VarDecl(span<Token> tok, string name, OptionalType type, AnyExpr init)
      : Decl(K_VarDecl, tok), name{move(name)}, type{move(type)}, init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto decl_name() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_VarDecl; }
  [[nodiscard]] auto clone() const -> VarDecl* override;
};

/// A declaration of a constant (`const`).
struct ConstDecl final : public Decl {
public:
  string name;
  AnyType type; // TODO(rymiel): make optional?
  AnyExpr init;

  ConstDecl(span<Token> tok, string name, AnyType type, AnyExpr init)
      : Decl(K_ConstDecl, tok), name{move(name)}, type{move(type)}, init(move(init)) {}
  void visit(Visitor& visitor) const override;
  [[nodiscard]] auto decl_name() const -> string override { return name; }

  static auto classof(const AST* a) -> bool { return a->kind() == K_ConstDecl; }
  [[nodiscard]] auto clone() const -> ConstDecl* override;
};

/// A while loop (`while`).
struct WhileStmt final : public Stmt {
public:
  AnyExpr cond;
  Compound body;

  WhileStmt(span<Token> tok, AnyExpr cond, Compound body) : Stmt(K_While, tok), cond{move(cond)}, body{move(body)} {}
  void visit(Visitor& visitor) const override;

  static auto classof(const AST* a) -> bool { return a->kind() == K_While; }
  [[nodiscard]] auto clone() const -> WhileStmt* override;
};

/// Clauses of an if statement `IfStmt`.
struct IfClause final : public AST {
public:
  AnyExpr cond;
  Compound body;

  IfClause(span<Token> tok, AnyExpr cond, Compound body) : AST(K_IfClause, tok), cond{move(cond)}, body{move(body)} {}
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
