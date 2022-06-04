//
// Created by rymiel on 5/22/22.
//

#include "type.hpp"

namespace yume::ty {
auto Type::known_qual(ast::QualType::Qualifier qual) -> Type& {
  auto existing = m_known_qual.find(qual);
  if (existing != m_known_qual.end()) {
    return *existing->second;
  }

  auto new_qual_type = std::make_unique<QualType>(*this, qual);
  auto r = m_known_qual.insert({qual, move(new_qual_type)});
  return *r.first->second;
}

auto Type::compatibility(const Type& other) const -> int {
  if (this == &other) {
    return 2;
  }
  if (m_kind == Kind::Integer && m_kind == other.m_kind) {
    const auto& this_integer = dynamic_cast<const IntegerType&>(*this);
    const auto& other_integer = dynamic_cast<const IntegerType&>(other);
    if (this_integer.is_signed() == other_integer.is_signed() && this_integer.size() <= other_integer.size()) {
      return 1;
    }
  }
  // TODO
  return 0;
}

auto Type::is_mut() const -> bool {
  return kind() == ty::Kind::Qual && dynamic_cast<const ty::QualType&>(*this).qualifier() == ast::QualType::Qualifier::Mut;
}
} // namespace yume::ty
