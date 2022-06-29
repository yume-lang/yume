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

auto Type::determine_generic_substitution(const Type& generic, Sub sub) const -> Sub {
  assert(generic.is_generic() && "Cannot substitute generics in a non-generic type"); // NOLINT

  // `Foo ptr` -> `T ptr`, with `T = Foo`.
  if (auto this_ptr_base = ptr_base(), gen_ptr_base = generic.ptr_base();
      this_ptr_base != nullptr && gen_ptr_base != nullptr &&
      cast<Ptr>(this)->qualifier() == cast<Ptr>(generic).qualifier()) {
    return this_ptr_base->determine_generic_substitution(*gen_ptr_base, sub);
  }
  // `Foo mut` -> `T mut`, with `T = Foo`.
  if (is_mut() && generic.is_mut())
    return qual_base()->determine_generic_substitution(generic.without_qual(), sub);

  // `Foo[] mut` -> `T[]`, with `T = Foo`.
  if (is_mut() && !generic.is_mut())
    return qual_base()->determine_generic_substitution(generic, sub);

  // Substitution impossible! For example, `Foo` -> `T ptr`.
  if (!isa<Generic>(generic))
    return sub;

  // Any other generic that didn't match above.
  // `Foo ptr` -> `T`, with `T = Foo ptr`.
  sub.target = &cast<Generic>(generic);
  sub.replace = this;
  return sub;
}

auto Type::compatibility(const Type& other, Compat compat) const -> Compat {
  if (this == &other) {
    compat.valid = true;
    return compat;
  }

  // `Foo mut` -> `Foo`.
  // Note that the base types are also compared, so `I32 mut` -> `I64`.
  if (is_mut() && !other.is_mut()) {
    compat.conv.dereference = true;
    compat = qual_base()->compatibility(other.without_qual(), compat);
    return compat;
  }

  // `I32` -> `I64`. `U8` -> `I16`. An implicit integer cast with no loss of information.
  if (const auto this_int = dyn_cast<Int>(this), other_int = dyn_cast<Int>(&other);
      (this_int != nullptr) && (other_int != nullptr)) {
    if ((this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size()) ||
        (this_int->is_signed() && !other_int->is_signed() && this_int->size() * 2 >= other_int->size())) {
      compat.valid = true;
      compat.conv.kind = Conv::Int;
      return compat;
    }
  }
  return compat;
}

auto Type::is_mut() const -> bool { return isa<Qual>(*this) && cast<Qual>(this)->has_qualifier(Qualifier::Mut); }

auto Type::is_generic() const -> bool {
  if (llvm::isa<Generic>(*this))
    return true;

  if (llvm::isa<Qual>(*this))
    return qual_base()->is_generic();

  if (llvm::isa<Ptr>(*this))
    return ptr_base()->is_generic();

  return false;
}

auto Type::apply_generic_substitution(Sub sub) const -> const Type* {
  assert(is_generic() && "Can't perform generic substitution without a generic type"); // NOLINT
  assert(sub.target != nullptr && sub.replace != nullptr &&                            // NOLINT
         "Can't perform generic substitution without anything to substitute");
  if (llvm::isa<Generic>(*this))
    return sub.replace;

  if (const auto* qual_this = llvm::dyn_cast<Qual>(this))
    if (qual_this->m_mut)
      return &qual_base()->apply_generic_substitution(sub)->known_mut();

  if (const auto* ptr_this = llvm::dyn_cast<Ptr>(this))
    return &ptr_base()->apply_generic_substitution(sub)->known_qual(ptr_this->qualifier());

  return nullptr;
}

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
