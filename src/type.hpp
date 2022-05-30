//
// Created by rymiel on 5/22/22.
//

#ifndef YUME_CPP_TYPE_HPP
#define YUME_CPP_TYPE_HPP

#include "ast.hpp"
#include "util.hpp"
#include <map>

namespace yume::ty {
enum struct Kind { Integer, Qual, Unknown };
class QualType;

class Type {
  std::map<ast::QualType::Qualifier, unique_ptr<Type>> m_known_qual{};

public:
  Type(Type&) noexcept = delete;
  Type(Type&&) noexcept = default;
  auto operator=(Type&) noexcept -> Type& = delete;
  auto operator=(Type&&) noexcept -> Type& = default;
  virtual ~Type() = default;
  [[nodiscard]] virtual auto kind() const -> Kind = 0;
  [[nodiscard]] auto known_qual(ast::QualType::Qualifier qual) -> Type&;

protected:
  constexpr Type() = default;
};

class IntegerType : public Type {
  int m_size;
  bool m_signed;

public:
  inline IntegerType(int size, bool signed_) : m_size(size), m_signed(signed_) {}
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Integer; }
  [[nodiscard]] inline auto size() const -> int { return m_size; }
  [[nodiscard]] inline auto is_signed() const -> bool { return m_signed; }
};

class QualType : public Type {
private:
  const Type& m_base;
  ast::QualType::Qualifier m_qualifier;

public:
  inline QualType(const Type& base, ast::QualType::Qualifier qualifier) : m_base(base), m_qualifier(qualifier) {}
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Qual; }
  [[nodiscard]] inline auto base() const -> const Type& { return m_base; }
  [[nodiscard]] inline auto qualifier() const -> ast::QualType::Qualifier { return m_qualifier; }
};

class UnknownType : public Type {
public:
  [[nodiscard]] inline auto kind() const -> Kind override { return Kind::Unknown; }
};
} // namespace yume::ty

#endif // YUME_CPP_TYPE_HPP
