//
// Created by rymiel on 5/22/22.
//

#ifndef YUME_CPP_TYPE_HPP
#define YUME_CPP_TYPE_HPP

#include "ast.hpp"
#include "util.hpp"
#include <llvm/IR/DerivedTypes.h>
#include <map>

namespace yume {
class Compiler;
}
namespace yume::ty {
enum struct Kind { Integer, Qual, Struct, Unknown };
class QualType;

class Type {
  std::map<ast::QualType::Qualifier, unique_ptr<Type>> m_known_qual{};
  const Kind m_kind;

public:
  Type(const Type&) noexcept = delete;
  Type(Type&&) noexcept = delete;
  auto operator=(const Type&) noexcept -> Type& = delete;
  auto operator=(Type&&) noexcept -> Type& = delete;
  virtual ~Type() = default;
  [[nodiscard]] auto kind() const -> Kind { return m_kind; };
  [[nodiscard]] auto known_qual(ast::QualType::Qualifier qual) -> Type&;
  [[nodiscard]] auto compatibility(const Type& other) const -> int;
  [[nodiscard]] inline auto known_ptr() -> Type& {
    return known_qual(ast::QualType::Qualifier::Ptr);
  }

  using enum Kind;

protected:
  Type(Kind kind) : m_kind(kind) {}
};

class IntegerType : public Type {
  int m_size;
  bool m_signed;

public:
  inline IntegerType(int size, bool signed_) : Type(Integer), m_size(size), m_signed(signed_) {}
  [[nodiscard]] inline auto size() const -> int { return m_size; }
  [[nodiscard]] inline auto is_signed() const -> bool { return m_signed; }
};

class QualType : public Type {
private:
  const Type& m_base;
  ast::QualType::Qualifier m_qualifier;

public:
  inline QualType(const Type& base, ast::QualType::Qualifier qualifier) : Type(Qual), m_base(base), m_qualifier(qualifier) {}
  [[nodiscard]] inline auto base() const -> const Type& { return m_base; }
  [[nodiscard]] inline auto qualifier() const -> ast::QualType::Qualifier { return m_qualifier; }
};

class StructType : public Type {
  string m_name;
  vector<const ast::TypeName*> m_fields;
  mutable llvm::StructType* m_memo{};
  inline void memo(llvm::StructType* memo) const { m_memo = memo; }

  friend Compiler;

public:
  inline StructType(string name, vector<const ast::TypeName*> fields) : Type(Struct), m_name(move(name)), m_fields(move(fields)) {}
  [[nodiscard]] inline auto name() const -> const auto& { return m_name; }
  [[nodiscard]] inline auto fields() const { return dereference_view(m_fields); }
  [[nodiscard]] inline auto memo() const -> auto* { return m_memo; }
};

class UnknownType : public Type {
public:
  inline UnknownType() : Type(Unknown) {}
};
} // namespace yume::ty

#endif // YUME_CPP_TYPE_HPP
