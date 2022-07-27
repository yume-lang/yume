#pragma once

#include "compatibility.hpp"
#include "qualifier.hpp"
#include "ty/substitution.hpp"
#include "ty/type_base.hpp"
#include "util.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class StructType;
}
namespace yume {
class Compiler;
struct Instantiation;
namespace ast {
struct TypeName;
}
} // namespace yume

namespace yume::ty {
class Type;
enum Kind {
  K_Unknown,             ///< `UnknownType`, default, zero value. Hopefully never encountered!
  K_Int,                 ///< `Int`
  K_Qual [[deprecated]], ///< `Qual`
  K_Ptr,                 ///< `Ptr`
  K_Struct,              ///< `Struct`
  K_Generic,             ///< `Generic`
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
class [[deprecated]] BaseType {
  [[deprecated]] mutable array<unique_ptr<BaseType>, static_cast<int>(Qualifier::Q_END)> m_known_ptr_like{};
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

  /// Get this type with a given qualifier applied.
  [[nodiscard, deprecated]] auto known_qual(Qualifier qual) const -> const BaseType&;
  [[nodiscard, deprecated]] auto known_ptr() const -> const BaseType& { return known_qual(Qualifier::Ptr); }
  [[nodiscard, deprecated]] auto known_mut() const -> const BaseType& { return known_qual(Qualifier::Mut); }
  [[nodiscard, deprecated]] auto known_slice() const -> const BaseType& { return known_qual(Qualifier::Slice); }

  [[nodiscard]] auto determine_generic_subs(const BaseType& generic, Substitution& subs) const -> Sub;
  [[nodiscard]] auto apply_generic_substitution(Sub sub) const -> const BaseType*;
  [[nodiscard, deprecated]] auto compatibility(const BaseType& other, Compat compat = Compat()) const -> Compat;
  /// The union of this and `other`. For example, the union of `T` and `T mut` is `T mut`.
  /// \returns `nullptr` if an union cannot be created.
  [[nodiscard, deprecated]] auto coalesce(const BaseType& other) const -> const BaseType*;
  /// Return the intersection of this and `other`. For example, the intersection of `T` and `T mut` is `T`.
  /// \returns `nullptr` is there's not intersection between the types.
  [[nodiscard, deprecated]] auto intersect(const BaseType& other) const -> const BaseType*;

  [[nodiscard, deprecated]] auto is_mut() const -> bool;
  [[nodiscard, deprecated]] auto is_generic() const -> bool;

  /// If this type is a mutable reference, return the base of it (`T mut` -> `T`)
  /// \returns `nullptr` if this type isn't a mutable reference.
  [[nodiscard, deprecated]] auto mut_base() const -> const BaseType*;

  /// If this type is a pointer type, return the base of it (`T ptr` -> `T`)
  /// \returns `nullptr` if this type isn't a pointer type.
  [[nodiscard, deprecated]] auto ptr_base() const -> const BaseType*;

  /// If this type is a mutable reference, return the base of it, otherwise return itself.
  [[nodiscard, deprecated]] auto without_mut() const -> const BaseType&;

protected:
  BaseType(Kind kind, string name) : m_kind(kind), m_name(move(name)) {}
};

/// A "qualified" type, with a non-stackable qualifier, \e .i.e. `mut`.
class Type {
  nullable<const BaseType*> m_base;
  bool m_mut{};

public:
  Type(nullable<const BaseType*> base, bool mut) : m_base(base), m_mut(mut) {}
  Type(nullable<const BaseType*> base) : m_base(base) {}

  auto operator==(const Type&) const -> bool = default;

  [[nodiscard]] auto kind() const -> Kind { return m_base->kind(); };
  [[nodiscard]] auto base() const -> nullable<const BaseType*> { return m_base; }
  template <typename T> [[nodiscard]] auto base_cast() const -> nullable<const T*> { return cast<T>(m_base); }
  template <typename T> [[nodiscard]] auto base_dyn_cast() const -> nullable<const T*> { return dyn_cast<T>(m_base); }
  template <typename T> [[nodiscard]] auto base_isa() const -> bool { return isa<T>(m_base); }
  [[nodiscard, deprecated]] auto has_qualifier(Qualifier qual) const -> bool {
    return (qual == Qualifier::Mut && m_mut);
  }
  [[nodiscard]] auto name() const -> string;

  [[nodiscard]] auto compatibility(Type other, Compat compat = Compat()) const -> Compat;

  /// Get this type with a given qualifier applied.
  [[nodiscard]] auto known_qual(Qualifier qual) const -> Type;
  [[nodiscard]] auto known_ptr() const -> Type { return known_qual(Qualifier::Ptr); }
  [[nodiscard]] auto known_mut() const -> Type { return known_qual(Qualifier::Mut); }
  [[nodiscard]] auto known_slice() const -> Type { return known_qual(Qualifier::Slice); }

  /// The union of this and `other`. For example, the union of `T` and `T mut` is `T mut`.
  /// \returns `nullopt` if an union cannot be created.
  [[nodiscard]] auto coalesce(Type other) const -> optional<Type>;
  /// Return the intersection of this and `other`. For example, the intersection of `T` and `T mut` is `T`.
  /// \returns `nullopt` is there's not intersection between the types.
  [[nodiscard]] auto intersect(Type other) const -> optional<Type>;

  [[nodiscard]] auto is_mut() const -> bool { return m_mut; };
  [[nodiscard]] auto is_generic() const -> bool;

  /// If this type is a mutable reference, return the base of it (`T mut` -> `T`)
  /// \returns `nullopt` if this type isn't a mutable reference.
  [[nodiscard]] auto mut_base() const -> optional<Type>;

  /// If this type is a pointer type, return the base of it (`T ptr` -> `T`)
  /// \returns `nullopt` if this type isn't a pointer type.
  [[nodiscard]] auto ptr_base() const -> optional<Type>;

  /// If this type is a mutable reference, return the base of it, otherwise return itself.
  [[nodiscard]] auto without_mut() const -> Type;
};

/// A built-in integral type, such as I32 or Bool.
class Int : public BaseType {
  int m_size;
  bool m_signed;

public:
  Int(string name, int size, bool is_signed) : BaseType(K_Int, move(name)), m_size(size), m_signed(is_signed) {}
  [[nodiscard]] auto size() const -> int { return m_size; }
  [[nodiscard]] auto is_signed() const -> bool { return m_signed; }
  [[nodiscard]] auto name() const -> string override { return base_name(); };
  [[nodiscard]] auto in_range(int64_t num) const -> bool;
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Int; }
};

/// A "qualified" type, with a non-stackable qualifier, \e .i.e. `mut`.
class [[deprecated]] Qual : public BaseType {
  friend BaseType;

private:
  const BaseType& m_base;
  bool m_mut{};

public:
  Qual(string name, const BaseType& base, bool mut) : BaseType(K_Qual, move(name)), m_base(base), m_mut(mut) {}
  [[nodiscard]] auto base() const -> const BaseType& { return m_base; }
  [[nodiscard]] auto has_qualifier(Qualifier qual) const -> bool { return (qual == Qualifier::Mut && m_mut); }
  [[nodiscard]] auto name() const -> string override;
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Qual; }
};

/// A "qualified" type, with a stackable qualifier, \e i.e. `ptr`.
class Ptr : public BaseType {
  friend BaseType;

private:
  Type m_base;
  Qualifier m_qual;

public:
  Ptr(string name, Type base, Qualifier qual) : BaseType(K_Ptr, move(name)), m_base(base), m_qual(qual) {}
  [[nodiscard, deprecated]] auto base() const -> const BaseType&;
  [[nodiscard]] auto pointee() const -> Type { return m_base; }
  [[nodiscard]] auto qualifier() const -> Qualifier { return m_qual; }
  [[nodiscard]] auto has_qualifier(Qualifier qual) const -> bool { return m_qual == qual; }
  [[nodiscard]] auto name() const -> string override;
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Ptr; }
};

/// An user-defined struct type with associated fields.
class Struct : public BaseType {
  vector<const ast::TypeName*> m_fields;
  const Substitution* m_subs;
  const Struct* m_parent{};
  mutable std::map<Substitution, unique_ptr<Struct>> m_subbed{}; // HACK
  auto emplace_subbed(Substitution sub) const -> const Struct&;
  mutable llvm::StructType* m_memo{};
  void memo(llvm::StructType* memo) const { m_memo = memo; }

  friend Compiler;
  friend BaseType;
  friend Type;

public:
  Struct(string name, vector<const ast::TypeName*> fields, const Substitution* subs)
      : BaseType(K_Struct, move(name)), m_fields(move(fields)), m_subs(subs) {}
  [[nodiscard]] auto fields() const -> const auto& { return m_fields; }
  [[nodiscard]] auto fields() -> auto& { return m_fields; }
  [[nodiscard]] auto subs() const -> const auto& { return *m_subs; }
  [[nodiscard]] auto name() const -> string override;
  [[nodiscard]] auto memo() const -> auto* { return m_memo; }
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Struct; }
};

/// An unsubstituted generic type variable, usually something like `T`.
///
/// Note that two different functions with the same name for a type variable use two different instances of `Generic`.
class Generic : public BaseType {
public:
  explicit Generic(string name) : BaseType(K_Generic, move(name)) {}
  [[nodiscard]] auto name() const -> string override { return base_name(); };
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Generic; }
};
} // namespace yume::ty
