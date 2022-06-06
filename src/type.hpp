//
// Created by rymiel on 5/22/22.
//

#ifndef YUME_CPP_TYPE_HPP
#define YUME_CPP_TYPE_HPP

#include "util.hpp"
#include "llvm/Support/Casting.h"
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
enum struct Qualifier { Ptr, Slice, Mut };
namespace ast {
class TypeName;
}
} // namespace yume

namespace yume::ty {
  using llvm::isa;
  using llvm::dyn_cast;
  using llvm::cast;

enum Kind {
  K_Unknown,
  K_Int,
  K_Qual,
  K_Struct,
};

class Type {
  std::map<Qualifier, unique_ptr<Type>> m_known_qual{};
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

  [[nodiscard]] auto known_qual(Qualifier qual) -> Type&;
  [[nodiscard]] inline auto known_ptr() -> Type& { return known_qual(Qualifier::Ptr); }
  [[nodiscard]] inline auto known_mut() -> Type& { return known_qual(Qualifier::Mut); }

  [[nodiscard]] auto compatibility(const Type& other) const -> int;
  [[nodiscard]] auto coalesce(Type& other) -> Type*;

  [[nodiscard]] auto is_mut() const -> bool;
  [[nodiscard]] auto qual_base() const -> Type*;
  [[nodiscard]] auto mut_base_or_this() const -> const Type&;
  [[nodiscard]] auto mut_base_or_this() -> Type&;
  [[nodiscard]] auto mut_base_or_this_kind() const -> Kind;

protected:
  Type(Kind kind, string name) : m_kind(kind), m_name(move(name)) {}
};

class Int : public Type {
  int m_size;
  bool m_signed;

public:
  inline Int(string name, int size, bool signed_)
      : Type(K_Int, move(name)), m_size(size), m_signed(signed_) {}
  [[nodiscard]] inline auto size() const -> int { return m_size; }
  [[nodiscard]] inline auto is_signed() const -> bool { return m_signed; }
  static auto classof(const Type* a) -> bool { return a->kind() == K_Int; }
};

class Qual : public Type {
private:
  Type& m_base;
  Qualifier m_qualifier;

public:
  inline Qual(string name, Type& base, Qualifier qualifier)
      : Type(K_Qual, move(name)), m_base(base), m_qualifier(qualifier) {}
  [[nodiscard]] inline auto base() const -> Type& { return m_base; }
  [[nodiscard]] inline auto qualifier() const -> Qualifier { return m_qualifier; }
  static auto classof(const Type* a) -> bool { return a->kind() == K_Qual; }
};

class Struct : public Type {
  vector<const ast::TypeName*> m_fields;
  mutable llvm::StructType* m_memo{};
  inline void memo(llvm::StructType* memo) const { m_memo = memo; }

  friend Compiler;

public:
  inline Struct(string name, vector<const ast::TypeName*> fields)
      : Type(K_Struct, move(name)), m_fields(move(fields)) {}
  [[nodiscard]] inline auto fields() const { return dereference_view(m_fields); }
  [[nodiscard]] inline auto memo() const -> auto* { return m_memo; }
  static auto classof(const Type* a) -> bool { return a->kind() == K_Struct; }
};

class UnknownType : public Type {
public:
  inline UnknownType() : Type(K_Unknown, "?") {}
  static auto classof(const Type* a) -> bool { return a->kind() == K_Unknown; }
};
} // namespace yume::ty

#endif // YUME_CPP_TYPE_HPP
