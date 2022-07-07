#pragma once

#include "compatibility.hpp"
#include "compiler/vals.hpp"
#include "qualifier.hpp"
#include "util.hpp"
#include "llvm/Support/Casting.h"
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {
class StructType;
}
namespace yume {
class Compiler;
namespace ast {
class TypeName;
}
} // namespace yume

namespace yume::ty {
enum Kind {
  K_Unknown, ///< `UnknownType`, default, zero value. Hopefully never encountered!
  K_Int,     ///< `Int`
  K_Qual,    ///< `Qual`
  K_Ptr,     ///< `Ptr`
  K_Struct,  ///< `Struct`
  K_Generic, ///< `Generic`
};

/// Represents a type in the type system. All types inherit from this class.
/**
 * Similar to `AST` nodes, `Type`s cannot be copied or moved as all special member functions (aside from the destructor)
 * are `delete`d. There will always be one instance of a `Type` object for a specific type. This means that comparing if
 * two types are the same type is simply a pointer equality check. The single instance of most types is located in
 * `TypeHolder`, except for "qualified" types, which are located in their base type.
 *
 * This means that a type such as `I32 ptr`, which is a `Ptr`, is stored in the `m_known_qual` array of the `I32` type,
 * at the index `Qualifier::Ptr`.
 */
class Type {
  mutable array<unique_ptr<Type>, static_cast<int>(Qualifier::Q_END)> m_known_qual{};
  const Kind m_kind;
  string m_name;

public:
  Type(const Type&) noexcept = delete;
  Type(Type&&) noexcept = delete;
  auto operator=(const Type&) noexcept -> Type& = delete;
  auto operator=(Type&&) noexcept -> Type& = delete;
  virtual ~Type() = default;
  [[nodiscard]] auto kind() const -> Kind { return m_kind; };
  [[nodiscard]] auto name() const -> string { return m_name; };

  /// Get this type with a given qualifier applied.
  /**
   * This may construct a new `Qual` or `Ptr` type, or use a cached instance from `m_known_qual`. Note that since some
   * qualifiers, such as `mut` don't stack. Getting the `mut`-qualified type of `T mut` returns itself.
   */
  [[nodiscard]] auto known_qual(Qualifier qual) const -> const Type&;
  [[nodiscard]] auto known_ptr() const -> const Type& { return known_qual(Qualifier::Ptr); }
  [[nodiscard]] auto known_mut() const -> const Type& { return known_qual(Qualifier::Mut); }
  [[nodiscard]] auto known_slice() const -> const Type& { return known_qual(Qualifier::Slice); }

  [[nodiscard]] auto determine_generic_substitution(const Type& generic, Sub sub = Sub()) const -> Sub;
  [[nodiscard]] auto apply_generic_substitution(Sub sub) const -> const Type*;
  [[nodiscard]] auto fully_apply_instantiation(const Instantiation& inst) const -> const Type*;
  [[nodiscard]] auto compatibility(const Type& other, Compat compat = Compat()) const -> Compat;
  /// The union of this and `other`. For example, the union of `T` and `T mut` is `T mut`.
  /// \returns `nullptr` if an union cannot be created.
  [[nodiscard]] auto coalesce(const Type& other) const -> const Type*;
  /// Return the intersection of this and `other`. For example, the intersection of `T and `T mut` is `T`.
  /// \returns `nullptr` is there's not intersection between the types.
  [[nodiscard]] auto intersect(const Type& other) const -> const Type*;

  [[nodiscard]] auto is_mut() const -> bool;
  [[nodiscard]] auto is_generic() const -> bool;

  /// If this type is a `Qual`, return the base of it (`T mut` -> `T`)
  /// \returns `nullptr` if this type isn't `Qual`.
  [[nodiscard]] auto qual_base() const -> const Type*;

  /// If this type is a `Ptr`, return the base of it (`T ptr` -> `T`)
  /// \returns `nullptr` if this type isn't `Ptr`.
  [[nodiscard]] auto ptr_base() const -> const Type*;

  /// If this type is a `Qual`, return the base of it, otherwise return itself.
  [[nodiscard]] auto without_qual() const -> const Type&;
  [[nodiscard]] auto without_qual_kind() const -> Kind;

protected:
  Type(Kind kind, string name) : m_kind(kind), m_name(move(name)) {}
};

/// A built-in integral type, such as I32 or Bool.
class Int : public Type {
  int m_size;
  bool m_signed;

public:
  Int(string name, int size, bool is_signed) : Type(K_Int, move(name)), m_size(size), m_signed(is_signed) {}
  [[nodiscard]] auto size() const -> int { return m_size; }
  [[nodiscard]] auto is_signed() const -> bool { return m_signed; }
  [[nodiscard]] auto in_range(int64_t num) const -> bool;
  static auto classof(const Type* a) -> bool { return a->kind() == K_Int; }
};

/// A "qualified" type, with a non-stackable qualifier, \e .i.e. `mut`.
class Qual : public Type {
  friend Type;

private:
  const Type& m_base;
  bool m_mut{};

public:
  Qual(string name, const Type& base, bool mut) : Type(K_Qual, move(name)), m_base(base), m_mut(mut) {}
  [[nodiscard]] auto base() const -> const Type& { return m_base; }
  [[nodiscard]] auto has_qualifier(Qualifier qual) const -> bool { return (qual == Qualifier::Mut && m_mut); }
  static auto classof(const Type* a) -> bool { return a->kind() == K_Qual; }
};

/// A "qualified" type, with a stackable qualifier, \e i.e. `ptr`.
class Ptr : public Type {
  friend Type;

private:
  const Type& m_base;
  Qualifier m_qual;

public:
  Ptr(string name, const Type& base, Qualifier qual) : Type(K_Ptr, move(name)), m_base(base), m_qual(qual) {}
  [[nodiscard]] auto base() const -> const Type& { return m_base; }
  [[nodiscard]] auto qualifier() const -> Qualifier { return m_qual; }
  [[nodiscard]] auto has_qualifier(Qualifier qual) const -> bool { return m_qual == qual; }
  static auto classof(const Type* a) -> bool { return a->kind() == K_Ptr; }
};

/// An user-defined struct type with associated fields.
class Struct : public Type {
  vector<const ast::TypeName*> m_fields;
  mutable llvm::StructType* m_memo{};
  void memo(llvm::StructType* memo) const { m_memo = memo; }

  friend Compiler;

public:
  Struct(string name, vector<const ast::TypeName*> fields) : Type(K_Struct, move(name)), m_fields(move(fields)) {}
  [[nodiscard]] auto fields() const { return dereference_view(m_fields); }
  [[nodiscard]] auto memo() const -> auto* { return m_memo; }
  static auto classof(const Type* a) -> bool { return a->kind() == K_Struct; }
};

/// An unsubstituted generic type variable, usually something like `T`.
/**
 * Note that two different functions with the same name for a type variable use two different instances of `Generic`.
 */
class Generic : public Type {
public:
  explicit Generic(string name) : Type(K_Generic, move(name)) {}
  static auto classof(const Type* a) -> bool { return a->kind() == K_Generic; }
};
} // namespace yume::ty
