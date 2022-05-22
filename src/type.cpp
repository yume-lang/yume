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
} // namespace yume::ty
