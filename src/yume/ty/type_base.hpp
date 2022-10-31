#pragma once

#include "qualifier.hpp"
#include "ty/compatibility.hpp"
#include "util.hpp"

namespace yume {
struct Substitution;
}

namespace yume::ty {
struct Sub;
class Type;

enum Kind {
  K_Unknown,    ///< `UnknownType`, default, zero value. Hopefully never encountered!
  K_Int,        ///< `Int`
  K_Nil,        ///< `Nil`
  K_Ptr,        ///< `Ptr`
  K_Struct,     ///< `Struct`
  K_Function,   ///< `Function`
  K_Generic,    ///< `Generic`
  K_OpaqueSelf, ///< `OpaqueSelf`
};

/// Represents a type in the type system.
/// NOTE: that this isn't the class to use for type introspection, for that, `Type` should be used instead as it is
/// fully qualified.
///
/// All kinds of types inherit from this class. Similar to `AST` nodes, `Type`s cannot be copied or moved as all special
/// member functions (aside from the destructor) are `delete`d.
///
/// There will always be one instance of a `Type` object for a specific type. This means that comparing if two types are
/// the same type is simply a pointer equality check.
///
/// The single instance of most types is located in `TypeHolder`, however "qualified" types are instead stored in
/// instances of `Type`. A "qualified" type is, for example, `mut`. These qualifiers do not stack. Getting the "mut"
/// reference to a mutable reference should be itself.
///
/// Also, pointer-like types are also not held in `TypeHolder`, and are instead stored in the `m_known_ptr_like` array
/// of their pointee type.
class BaseType {
  mutable array<unique_ptr<BaseType>, static_cast<int>(PtrLikeQualifier::Q_END)> m_known_ptr_like{};
  const Kind m_kind;
  string m_name;

  friend Type;

public:
  BaseType(const BaseType&) noexcept = delete;
  BaseType(BaseType&&) noexcept = delete;
  auto operator=(const BaseType&) noexcept -> BaseType& = delete;
  auto operator=(BaseType&&) noexcept -> BaseType& = delete;
  virtual ~BaseType() = default;
  [[nodiscard]] auto kind() const -> Kind { return m_kind; };
  [[nodiscard]] auto base_name() const -> string { return m_name; };
  [[nodiscard]] virtual auto name() const -> string = 0;

protected:
  BaseType(Kind kind, string name) : m_kind(kind), m_name(move(name)) {}
};

/// A "qualified" type, with a non-stackable qualifier, \e i.e. `mut`.
class Type {
  nonnull<const BaseType*> m_base;
  bool m_mut{};

public:
  Type(nonnull<const BaseType*> base, bool mut) noexcept : m_base(base), m_mut(mut) {}
  Type(nonnull<const BaseType*> base) noexcept : m_base(base) {}

  auto operator<=>(const Type&) const noexcept = default;
  [[nodiscard]] auto opaque_equal(const Type &other) const noexcept -> bool;

  [[nodiscard]] auto kind() const noexcept -> Kind { return m_base->kind(); };
  [[nodiscard]] auto base() const noexcept -> nonnull<const BaseType*> { return m_base; }
  template <typename T> [[nodiscard]] auto base_cast() const noexcept -> nonnull<const T*> { return cast<T>(m_base); }
  template <typename T> [[nodiscard]] auto base_dyn_cast() const noexcept -> nullable<const T*> {
    return dyn_cast<T>(m_base);
  }
  template <typename T> [[nodiscard]] auto try_as() const noexcept -> optional<Type> {
    const auto* cast = dyn_cast<T>(m_base);
    if (cast)
      return {cast};
    return std::nullopt;
  }
  template <typename T> [[nodiscard]] auto base_isa() const noexcept -> bool { return isa<T>(m_base); }
  [[nodiscard]] auto has_qualifier(Qualifier qual) const -> bool;
  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] auto base_name() const -> string;

  [[nodiscard]] auto determine_generic_subs(Type generic, Substitution& subs) const -> optional<Sub>;
  [[nodiscard]] auto apply_generic_substitution(Sub sub) const -> optional<Type>;
  [[nodiscard]] auto compatibility(Type other, Compat compat = Compat()) const -> Compat;

  /// Get this type with a given qualifier applied.
  [[nodiscard]] auto known_qual(Qualifier qual) const -> Type;
  [[nodiscard]] auto known_ptr() const -> Type { return known_qual(Qualifier::Ptr); }
  [[nodiscard]] auto known_mut() const -> Type { return known_qual(Qualifier::Mut); }

  /// The union of this and `other`. For example, the union of `T` and `T mut` is `T mut`.
  /// \returns `nullopt` if an union cannot be created.
  [[nodiscard]] auto coalesce(Type other) const noexcept -> optional<Type>;
  /// Return the intersection of this and `other`. For example, the intersection of `T` and `T mut` is `T`.
  /// \returns `nullopt` is there's not intersection between the types.
  [[nodiscard]] auto intersect(Type other) const noexcept -> optional<Type>;

  [[nodiscard]] auto is_mut() const noexcept -> bool { return m_mut; };
  [[nodiscard]] auto is_slice() const noexcept -> bool;
  [[nodiscard]] auto is_generic() const noexcept -> bool;
  [[nodiscard]] auto is_opaque_self() const noexcept -> bool;

  [[nodiscard]] auto is_trivially_destructible() const -> bool;

  /// If this type is a mutable reference, return the base of it (`T mut` -> `T`)
  /// \returns `nullopt` if this type isn't a mutable reference.
  [[nodiscard]] auto mut_base() const noexcept -> optional<Type>;

  /// If this type is a mutable reference, return the base of it (`T mut` -> `T`)
  /// \throws if this type isn't a mutable reference.
  [[nodiscard]] auto ensure_mut_base() const noexcept(false) -> Type;

  /// If this type is a pointer type, return the base of it (`T ptr` -> `T`)
  /// \returns `nullopt` if this type isn't a pointer type.
  [[nodiscard]] auto ptr_base() const noexcept -> optional<Type>;

  /// If this type is a pointer type, return the base of it (`T ptr` -> `T`)
  /// \throws if this type isn't a pointer type.
  [[nodiscard]] auto ensure_ptr_base() const noexcept(false) -> Type;

  /// If this type is a mutable reference, return the base of it, otherwise return itself.
  [[nodiscard]] auto without_mut() const noexcept -> Type;
};
} // namespace yume::ty
