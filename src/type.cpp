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

  auto new_qual_type = std::make_unique<Qual>(name() + qual_suffix(qual), *this, qual);
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
  if (const auto this_int = dyn_cast<Int>(this), other_int = dyn_cast<Int>(&other);
      (this_int != nullptr) && (other_int != nullptr)) {
    if (this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size()) {
      return SAFE_CONVERSION;
    }
  }
  // TODO
  return 0;
}

auto Type::is_mut() const -> bool {
  return isa<Qual>(*this) && cast<Qual>(this)->qualifier() == Qualifier::Mut;
}

auto Type::qual_base() const -> Type* {
  if (const auto* qual = dyn_cast<Qual>(this)) {
    return &qual->base();
  }
  return nullptr;
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
