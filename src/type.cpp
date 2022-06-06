//
// Created by rymiel on 5/22/22.
//

#include "type.hpp"

namespace yume::ty {
static auto qual_suffix(Qualifier qual) -> string {
  switch (qual) {
  case Qualifier::Mut: return " mut";
  case Qualifier::Ptr: return " ptr";
  case Qualifier::Slice: return "[]";
  default: return "";
  }
}
auto Type::known_qual(Qualifier qual) -> Type& {
  auto existing = m_known_qual.find(qual);
  if (existing != m_known_qual.end()) {
    return *existing->second;
  }

  auto new_qual_type = std::make_unique<QualType>(name() + qual_suffix(qual), *this, qual);
  auto r = m_known_qual.insert({qual, move(new_qual_type)});
  return *r.first->second;
}

static constinit const int PERFECT_MATCH = 100;
static constinit const int SAFE_CONVERSION = 10;

auto Type::compatibility(const Type& other) const -> int {
  if (this == &other) {
    return PERFECT_MATCH;
  }
  if (is_mut() && !other.is_mut() && qual_base() == &other) {
    return SAFE_CONVERSION;
  }
  if (m_kind == Kind::Integer && m_kind == other.m_kind) {
    const auto& this_integer = dynamic_cast<const IntegerType&>(*this);
    const auto& other_integer = dynamic_cast<const IntegerType&>(other);
    if (this_integer.is_signed() == other_integer.is_signed() && this_integer.size() <= other_integer.size()) {
      return SAFE_CONVERSION;
    }
  }
  // TODO
  return 0;
}

auto Type::is_mut() const -> bool {
  return kind() == ty::Kind::Qual && dynamic_cast<const ty::QualType&>(*this).qualifier() == Qualifier::Mut;
}

auto Type::qual_base() const -> Type* {
  if (kind() != ty::Kind::Qual) {
    return nullptr;
  }
  return &dynamic_cast<const ty::QualType&>(*this).base();
}

auto Type::coalesce(Type& other) -> Type* {
  if (is_mut() && qual_base() == &other) {
    return this;
  }
  if (other.is_mut() && other.qual_base() == this) {
    return &other;
  }
  return nullptr;
}

auto Type::mut_base_or_this() const -> const Type& { return *(is_mut() ? qual_base() : this); }

auto Type::mut_base_or_this() -> Type& { return *(is_mut() ? qual_base() : this); }

auto Type::mut_base_or_this_kind() const -> Kind { return (is_mut() ? qual_base() : this)->kind(); }
} // namespace yume::ty
