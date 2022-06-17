//
// Created by rymiel on 5/22/22.
//

#include "type.hpp"

#include <utility>

namespace yume::ty {
static auto qual_suffix(Qualifier qual) -> string {
  switch (qual) {
  case Qualifier::Mut: return " mut";
  case Qualifier::Ptr: return " ptr";
  case Qualifier::Scope: return " scope";
  case Qualifier::Slice: return "[]";
  default: return "";
  }
}

auto Type::known_qual(Qualifier qual) const -> const Type& {
  int qual_idx = static_cast<int>(qual);
  const auto& existing = m_known_qual.at(qual_idx);
  if (existing != nullptr) {
    return *existing;
  }

  const auto* base = qual_base();
  // Qualifiers like Mut and Scope can't repeat, so attempting to get mut of a mut type should return itself
  if (qual == Qualifier::Mut || qual == Qualifier::Scope) {
    bool existing_mut = false;
    bool existing_scope = false;
    const auto* existing_base = this;
    if (base != nullptr) {
      if (base->m_known_qual.at(qual_idx).get() == this) {
        return *this;
      }
      const auto* existing_qual = cast<Qual>(this);
      existing_mut = existing_qual->m_mut;
      existing_scope = existing_qual->m_scope;
      existing_base = &existing_qual->m_base;
    }
    auto new_qual_type =
        std::make_unique<Qual>(name() + qual_suffix(qual), *existing_base, (existing_mut || qual == Qualifier::Mut),
                               (existing_scope || qual == Qualifier::Scope));
    m_known_qual.at(qual_idx) = move(new_qual_type);
  } else {
    auto new_qual_type = std::make_unique<Ptr>(name() + qual_suffix(qual), *this, qual);
    m_known_qual.at(qual_idx) = move(new_qual_type);
  }
  return *m_known_qual.at(qual_idx);
}

static constinit const int PERFECT_MATCH = 1000;
static constinit const int SAFE_CONVERSION = 100;
static constinit const int GENERIC_SUBSTITUTION = 10;

auto Type::compatibility(const Type& other) const -> Compatiblity {
  if (this == &other) {
    return {PERFECT_MATCH};
  }
  if (!is_mut() && !is_scope() && isa<Generic>(other)) {
    return {GENERIC_SUBSTITUTION, &cast<Generic>(other), this};
  }
  if (auto this_ptr_base = without_qual().ptr_base(), other_ptr_base = other.without_qual().ptr_base();
      (((is_scope() || is_mut()) && !other.is_scope() && !other.is_mut()) || (is_scope() && other.is_mut())) &&
      this_ptr_base != nullptr && other_ptr_base != nullptr &&
      cast<Ptr>(without_qual()).qualifier() == cast<Ptr>(other.without_qual()).qualifier()) {
    if (isa<Generic>(other_ptr_base)) {
      return {GENERIC_SUBSTITUTION, cast<Generic>(other_ptr_base), this_ptr_base};
    }
    return {0};
  }
  if (auto this_ptr_base = ptr_base(), other_ptr_base = other.ptr_base();
      this_ptr_base != nullptr && other_ptr_base != nullptr) {
    if (isa<Generic>(other_ptr_base)) {
      return {GENERIC_SUBSTITUTION, cast<Generic>(other_ptr_base), this_ptr_base};
    }
    return {0};
  }
  if ((is_scope() && !other.is_scope()) || (is_mut() && !other.is_mut()) || (is_scope() && other.is_mut())) {
    auto base_compatibility = qual_base()->compatibility(other.without_qual());
    if (base_compatibility.rating != Compatiblity::INVALID) {
      return base_compatibility;
    }
  }
  if (const auto this_int = dyn_cast<Int>(this), other_int = dyn_cast<Int>(&other);
      (this_int != nullptr) && (other_int != nullptr)) {
    if (this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size()) {
      return {SAFE_CONVERSION};
    }
  }
  // TODO
  return {Compatiblity::INVALID};
}

auto Type::is_mut() const -> bool { return isa<Qual>(*this) && cast<Qual>(this)->has_qualifier(Qualifier::Mut); }

auto Type::is_scope() const -> bool { return isa<Qual>(*this) && cast<Qual>(this)->has_qualifier(Qualifier::Scope); }

auto Type::qual_base() const -> const Type* {
  if (const auto* qual = dyn_cast<Qual>(this)) {
    return &qual->base();
  }
  return nullptr;
}

auto Type::ptr_base() const -> const Type* {
  if (const auto* ptr = dyn_cast<Ptr>(this)) {
    return &ptr->base();
  }
  return nullptr;
}

auto Type::coalesce(const Type& other) const -> const Type* {
  if ((is_mut() || is_scope()) && qual_base() == &other) {
    return this;
  }
  if ((other.is_mut() || other.is_scope()) && other.qual_base() == this) {
    return &other;
  }
  return nullptr;
}

auto Type::without_qual() const -> const Type& { return *(isa<Qual>(*this) ? qual_base() : this); }

auto Type::without_qual_kind() const -> Kind { return (isa<Qual>(*this) ? qual_base() : this)->kind(); }
} // namespace yume::ty
