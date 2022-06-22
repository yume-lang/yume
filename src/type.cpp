#include "type.hpp"
#include <llvm/Support/Casting.h>
#include <utility>

namespace yume::ty {
static auto qual_suffix(Qualifier qual) -> string {
  switch (qual) {
  case Qualifier::Mut: return " mut";
  case Qualifier::Ptr: return " ptr";
  case Qualifier::Slice: return "[]";
  default: return "";
  }
}

auto Type::known_qual(Qualifier qual) const -> const Type& {
  int qual_idx = static_cast<int>(qual);
  const auto& existing = m_known_qual.at(qual_idx);
  if (existing != nullptr)
    return *existing;

  const auto* base = qual_base();
  // Qualifiers like Mut can't repeat, so attempting to get mut of a mut type should return itself
  if (qual == Qualifier::Mut) {
    bool existing_mut = false;
    const auto* existing_base = this;
    if (base != nullptr) {
      if (base->m_known_qual.at(qual_idx).get() == this)
        return *this;
      const auto* existing_qual = cast<Qual>(this);
      existing_mut = existing_qual->m_mut;
      existing_base = &existing_qual->m_base;
    }
    auto new_qual_type =
        std::make_unique<Qual>(name() + qual_suffix(qual), *existing_base, (existing_mut || qual == Qualifier::Mut));
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
  if (this == &other)
    return {PERFECT_MATCH};

  // `Foo[] mut` -> `T[]`, with `T = Foo`.
  if (auto this_ptr_base = without_qual().ptr_base(), other_ptr_base = other.without_qual().ptr_base();
      (is_mut() && !other.is_mut()) && this_ptr_base != nullptr && other_ptr_base != nullptr &&
      cast<Ptr>(without_qual()).qualifier() == cast<Ptr>(other.without_qual()).qualifier()) {
    if (isa<Generic>(other_ptr_base))
      return {GENERIC_SUBSTITUTION, cast<Generic>(other_ptr_base), this_ptr_base};
    return {0};
  }
  // `Foo ptr` -> `T ptr`, with `T = Foo`.
  if (auto this_ptr_base = ptr_base(), other_ptr_base = other.ptr_base();
      this_ptr_base != nullptr && other_ptr_base != nullptr &&
      cast<Ptr>(this)->qualifier() == cast<Ptr>(other).qualifier()) {
    if (isa<Generic>(other_ptr_base))
      return {GENERIC_SUBSTITUTION, cast<Generic>(other_ptr_base), this_ptr_base};
    return {0};
  }
  // `Foo mut` -> `Foo mut`.
  // Note that the base types are also compared, so `I32 mut` -> `I64 mut`.
  if (is_mut() && other.is_mut()) {
    auto base_compat = qual_base()->compatibility(other.without_qual());
    if (base_compat.rating != Compatiblity::INVALID)
      return base_compat + 1;
  }
  // Any other generic that didn't match above.
  // `Foo ptr` -> `T`, with `T = Foo ptr`.
  if (isa<Generic>(other))
    return {GENERIC_SUBSTITUTION, &cast<Generic>(other), this};
  // `Foo mut` -> `Foo`.
  // Note that the base types are also compared, so `I32 mut` -> `I64`.
  if (is_mut() && !other.is_mut()) {
    auto base_compat = qual_base()->compatibility(other);
    if (base_compat.rating != Compatiblity::INVALID)
      return base_compat + 1;
  }
  // `I32` -> `I64`. An implicit integer cast with no loss of information.
  // Note that the signs need to be the same, even when converting `U8` -> `I32`.
  // TODO: change this
  if (const auto this_int = dyn_cast<Int>(this), other_int = dyn_cast<Int>(&other);
      (this_int != nullptr) && (other_int != nullptr)) {
    if (this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size())
      return {SAFE_CONVERSION};
  }
  return {Compatiblity::INVALID};
}

auto Type::is_mut() const -> bool { return isa<Qual>(*this) && cast<Qual>(this)->has_qualifier(Qualifier::Mut); }

auto Type::qual_base() const -> const Type* {
  if (const auto* qual = dyn_cast<Qual>(this))
    return &qual->base();
  return nullptr;
}

auto Type::ptr_base() const -> const Type* {
  if (const auto* ptr = dyn_cast<Ptr>(this))
    return &ptr->base();
  return nullptr;
}

auto Type::coalesce(const Type& other) const -> const Type* {
  if (this == &other)
    return this;
  if (is_mut() && qual_base() == &other)
    return this;
  if (other.is_mut() && other.qual_base() == this)
    return &other;

  return nullptr;
}

auto Type::intersect(const Type& other) const -> const Type* {
  if (this == &other)
    return this;
  if (is_mut() && qual_base() == &other)
    return &other;
  if (other.is_mut() && other.qual_base() == this)
    return this;

  return nullptr;
}

auto Type::without_qual() const -> const Type& { return *(isa<Qual>(*this) ? qual_base() : this); }

auto Type::without_qual_kind() const -> Kind { return (isa<Qual>(*this) ? qual_base() : this)->kind(); }
} // namespace yume::ty
