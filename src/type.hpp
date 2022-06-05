//
// Created by rymiel on 5/22/22.
//

#ifndef YUME_CPP_TYPE_HPP
#define YUME_CPP_TYPE_HPP

#include "util.hpp"
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
namespace ast {
class TypeName;
}
} // namespace yume

namespace yume::ty {
enum struct Kind { Integer, Qual, Struct, Unknown };
enum struct Qualifier { Ptr, Slice, Mut };

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
  [[nodiscard]] auto compatibility(const Type& other) const -> int;
  [[nodiscard]] inline auto known_ptr() -> Type& { return known_qual(Qualifier::Ptr); }
  [[nodiscard]] auto is_mut() const -> bool;

  using enum Kind;

protected:
  Type(Kind kind, string name) : m_kind(kind), m_name(move(name)) {}
};

class IntegerType : public Type {
  int m_size;
  bool m_signed;

public:
  inline IntegerType(string name, int size, bool signed_)
      : Type(Integer, move(name)), m_size(size), m_signed(signed_) {}
  [[nodiscard]] inline auto size() const -> int { return m_size; }
  [[nodiscard]] inline auto is_signed() const -> bool { return m_signed; }
};

class QualType : public Type {
private:
  Type& m_base;
  Qualifier m_qualifier;

public:
  inline QualType(string name, Type& base, Qualifier qualifier)
      : Type(Qual, move(name)), m_base(base), m_qualifier(qualifier) {}
  [[nodiscard]] inline auto base() const -> Type& { return m_base; }
  [[nodiscard]] inline auto qualifier() const -> Qualifier { return m_qualifier; }
};

class StructType : public Type {
  vector<const ast::TypeName*> m_fields;
  mutable llvm::StructType* m_memo{};
  inline void memo(llvm::StructType* memo) const { m_memo = memo; }

  friend Compiler;

public:
  inline StructType(string name, vector<const ast::TypeName*> fields)
      : Type(Struct, move(name)), m_fields(move(fields)) {}
  [[nodiscard]] inline auto fields() const { return dereference_view(m_fields); }
  [[nodiscard]] inline auto memo() const -> auto* { return m_memo; }
};

class UnknownType : public Type {
public:
  inline UnknownType() : Type(Unknown, "?") {}
};
} // namespace yume::ty

#endif // YUME_CPP_TYPE_HPP
