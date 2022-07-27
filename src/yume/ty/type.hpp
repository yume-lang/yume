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
struct Sub;

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

/// A "qualified" type, with a stackable qualifier, \e i.e. `ptr`.
class Ptr : public BaseType {
  friend BaseType;

private:
  Type m_base;
  Qualifier m_qual;

public:
  Ptr(string name, Type base, Qualifier qual) : BaseType(K_Ptr, move(name)), m_base(base), m_qual(qual) {}
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
