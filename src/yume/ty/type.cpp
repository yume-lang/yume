#include "type.hpp"
#include "compiler/vals.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include <cstddef>
#include <limits>
#include <llvm/Support/Casting.h>
#include <map>
#include <sstream>
#include <stdexcept>
#include <type_traits>
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

auto BaseType::known_qual(Qualifier qual) const -> const BaseType& {
  int qual_idx = static_cast<int>(qual);
  const auto& existing = m_known_ptr_like.at(qual_idx);
  if (existing != nullptr)
    return *existing;

  const auto* base = mut_base();
  // Qualifiers like Mut can't repeat, so attempting to get mut of a mut type should return itself
  if (qual == Qualifier::Mut) {
    bool existing_mut = false;
    const auto* existing_base = this;
    if (base != nullptr) {
      if (base->m_known_ptr_like.at(qual_idx).get() == this)
        return *this;
      const auto* existing_qual = cast<Qual>(this);
      existing_mut = existing_qual->m_mut;
      existing_base = &existing_qual->m_base;
    }
    auto new_qual_type = std::make_unique<Qual>(m_name, *existing_base, (existing_mut || qual == Qualifier::Mut));
    m_known_ptr_like.at(qual_idx) = move(new_qual_type);
  } else {
    auto new_qual_type = std::make_unique<Ptr>(m_name, *this, qual);
    m_known_ptr_like.at(qual_idx) = move(new_qual_type);
  }
  return *m_known_ptr_like.at(qual_idx);
}

auto Type::known_qual(Qualifier qual) const -> Type {
  if (qual == Qualifier::Mut)
    Type(m_base, true);

  int qual_idx = static_cast<int>(qual);
  const auto& existing = m_base->m_known_ptr_like.at(qual_idx);
  if (existing != nullptr)
    return {existing.get()};

  auto new_qual_type = std::make_unique<Ptr>(m_base->base_name(), *m_base, qual);
  m_base->m_known_ptr_like.at(qual_idx) = move(new_qual_type);
  return {m_base->m_known_ptr_like.at(qual_idx).get()};
}

[[deprecated]] static auto visit_subs(const BaseType& a, const BaseType& b, Sub sub) -> Sub {
  yume_assert(b.is_generic(), "Cannot substitute generics in a non-generic type");

  // `Foo ptr` -> `T ptr`, with `T = Foo`.
  if (auto a_ptr_base = a.ptr_base(), b_ptr_base = b.ptr_base();
      a_ptr_base != nullptr && b_ptr_base != nullptr && cast<Ptr>(a).qualifier() == cast<Ptr>(b).qualifier()) {
    return visit_subs(*a_ptr_base, *b_ptr_base, sub);
  }
  // `Foo mut` -> `T mut`, with `T = Foo`.
  if (a.is_mut() && b.is_mut())
    return visit_subs(*a.mut_base(), b.without_mut(), sub);

  // `Foo ptr mut` -> `T ptr`, with `T = Foo`.
  if (a.is_mut() && !b.is_mut())
    return visit_subs(*a.mut_base(), b, sub);

  // `Foo{Bar}` -> `Foo{T}`, with `T = Foo`.
  // TODO(rymiel): This could technically have multiple type variables... Currently only handling the "1" case.
  if (auto a_st_ty = dyn_cast<Struct>(&a), b_st_ty = dyn_cast<Struct>(&b); a_st_ty != nullptr && b_st_ty != nullptr)
    if (a_st_ty->base_name() == b_st_ty->base_name())
      if (a_st_ty->subs().size() == 1 && b_st_ty->subs().size() == 1)
        return visit_subs(*a_st_ty->subs().begin()->second.base(), *b_st_ty->subs().begin()->second.base(), sub);

  // Substitution impossible! For example, `Foo` -> `T ptr`.
  if (!isa<Generic>(b))
    return sub;

  // Any other generic that didn't match above.
  // `Foo ptr` -> `T`, with `T = Foo ptr`.
  sub.target = &cast<Generic>(b);
  sub.replace = &a;
  return sub;
}

static auto visit_subs(Type a, Type b, optional<Sub> sub) -> optional<Sub> {
  yume_assert(b.is_generic(), "Cannot substitute generics in a non-generic type");

  // `Foo ptr` -> `T ptr`, with `T = Foo`.
  if (auto a_ptr_base = a.ptr_base(), b_ptr_base = b.ptr_base();
      a_ptr_base != nullptr && b_ptr_base != nullptr &&
      a.base_cast<Ptr>()->qualifier() == b.base_cast<Ptr>()->qualifier()) {
    return visit_subs(*a_ptr_base, *b_ptr_base, sub);
  }
  // `Foo mut` -> `T mut`, with `T = Foo`.
  if (a.is_mut() && b.is_mut())
    return visit_subs(*a.mut_base(), b.without_mut(), sub);

  // `Foo ptr mut` -> `T ptr`, with `T = Foo`.
  if (a.is_mut() && !b.is_mut())
    return visit_subs(*a.mut_base(), b, sub);

  // `Foo{Bar}` -> `Foo{T}`, with `T = Foo`.
  // TODO(rymiel): This could technically have multiple type variables... Currently only handling the "1" case.
  if (auto a_st_ty = a.base_dyn_cast<Struct>(), b_st_ty = b.base_dyn_cast<Struct>();
      a_st_ty != nullptr && b_st_ty != nullptr)
    if (a_st_ty->base_name() == b_st_ty->base_name())
      if (a_st_ty->subs().size() == 1 && b_st_ty->subs().size() == 1)
        return visit_subs(a_st_ty->subs().begin()->second, b_st_ty->subs().begin()->second, sub);

  // Substitution impossible! For example, `Foo` -> `T ptr`.
  if (!b.base_isa<Generic>())
    return sub;

  // Any other generic that didn't match above.
  // `Foo ptr` -> `T`, with `T = Foo ptr`.
  if (sub) {
    sub->target = b.base_cast<Generic>();
    sub->replace = a;
  }
  return sub;
}

/// Add the substitution `gen_sub` for the generic type variable `gen` in template instantiation `instantiation`.
/// If a substitution already exists for the same type variable, the two substitutions are intersected.
/// \returns nullptr if substitution failed
[[deprecated]] static auto intersect_generics(Substitution& subs, const Generic* gen, const BaseType* gen_sub)
    -> const BaseType* {
  if (gen == nullptr)
    return nullptr;

  auto name = gen->name();
  auto existing = subs.find(name);
  // The substitution must have an intersection with an already deduced value for the same type variable
  if (existing != subs.end()) {
    const auto* intersection = gen_sub->intersect(*existing->second.base());
    if (intersection == nullptr) {
      // The types don't have a common intersection, they cannot coexist.
      return nullptr;
    }
    subs.try_emplace(name, intersection);
  } else {
    subs.try_emplace(name, gen_sub);
  }

  return subs.at(name).base();
}

/// Add the substitution `gen_sub` for the generic type variable `gen` in template instantiation `instantiation`.
/// If a substitution already exists for the same type variable, the two substitutions are intersected.
/// \returns nullopt if substitution failed
static auto intersect_generics(Substitution& subs, const Generic* gen, ty::Type gen_sub) -> optional<ty::Type> {
  if (gen == nullptr)
    return {};

  auto name = gen->name();
  auto existing = subs.find(name);
  // The substitution must have an intersection with an already deduced value for the same type variable
  if (existing != subs.end()) {
    auto intersection = gen_sub.intersect(existing->second);
    if (!intersection) {
      // The types don't have a common intersection, they cannot coexist.
      return {};
    }
    subs.try_emplace(name, *intersection);
  } else {
    subs.try_emplace(name, gen_sub);
  }

  return subs.at(name).base();
}

auto BaseType::determine_generic_subs(const BaseType& generic, Substitution& subs) const -> Sub {
  yume_assert(generic.is_generic(), "Cannot substitute generics in a non-generic type");

  Sub sub{nullptr, nullptr};
  sub = visit_subs(*this, generic, sub);
  sub.replace = intersect_generics(subs, sub.target, sub.replace.base());

  return sub;
}

auto Type::determine_generic_subs(Type generic, Substitution& subs) const -> optional<Sub> {
  yume_assert(generic.is_generic(), "Cannot substitute generics in a non-generic type");

  optional<Sub> sub{};
  sub = visit_subs(*this, generic, sub);
  if (sub)
    if (auto intersection = intersect_generics(subs, sub->target, sub->replace))
      sub->replace = *intersection;

  return sub;
}

auto BaseType::compatibility(const BaseType& other, Compat compat) const -> Compat {
  if (this == &other) {
    compat.valid = true;
    return compat;
  }

  // `Foo mut` -> `Foo`.
  // Note that the base types are also compared, so `I32 mut` -> `I64`.
  if (is_mut() && !other.is_mut()) {
    compat.conv.dereference = true;
    compat = mut_base()->compatibility(other.without_mut(), compat);
    return compat;
  }

  // `I32` -> `I64`. `U8` -> `I16`. An implicit integer cast with no loss of information.
  if (const auto this_int = dyn_cast<Int>(this), other_int = dyn_cast<Int>(&other);
      (this_int != nullptr) && (other_int != nullptr)) {
    if ((this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size()) ||
        (!this_int->is_signed() && other_int->is_signed() && this_int->size() * 2 <= other_int->size())) {
      compat.valid = true;
      compat.conv.kind = Conv::Int;
      return compat;
    }
  }
  return compat;
}

auto Type::compatibility(Type other, Compat compat) const -> Compat {
  if (*this == other) {
    compat.valid = true;
    return compat;
  }

  // `Foo mut` -> `Foo`.
  // Note that the base types are also compared, so `I32 mut` -> `I64`.
  if (is_mut() && !other.is_mut()) {
    compat.conv.dereference = true;
    compat = mut_base()->compatibility(other.without_mut(), compat);
    return compat;
  }

  // `I32` -> `I64`. `U8` -> `I16`. An implicit integer cast with no loss of information.
  if (const auto this_int = base_dyn_cast<Int>(), other_int = other.base_dyn_cast<Int>();
      (this_int != nullptr) && (other_int != nullptr)) {
    if ((this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size()) ||
        (!this_int->is_signed() && other_int->is_signed() && this_int->size() * 2 <= other_int->size())) {
      compat.valid = true;
      compat.conv.kind = Conv::Int;
      return compat;
    }
  }
  return compat;
}

auto BaseType::is_mut() const -> bool { return isa<Qual>(*this) && cast<Qual>(this)->has_qualifier(Qualifier::Mut); }

auto BaseType::is_generic() const -> bool {
  if (isa<Generic>(*this))
    return true;

  if (isa<Qual>(*this))
    return mut_base()->is_generic();

  if (isa<Ptr>(*this))
    return ptr_base()->is_generic();

  if (const auto* struct_ty = dyn_cast<Struct>(this))
    return std::ranges::any_of(struct_ty->subs(), [](const auto& sub) { return sub.second.is_generic(); });
  // XXX: The above ranges call is repeated often

  return false;
}

auto Type::is_generic() const noexcept -> bool {
  if (base_isa<Generic>())
    return true;

  if (base_isa<Ptr>())
    return ptr_base()->is_generic();

  if (const auto* struct_ty = base_dyn_cast<Struct>())
    return std::ranges::any_of(struct_ty->subs(), [](const auto& sub) { return sub.second.is_generic(); });
  // XXX: The above ranges call is repeated often

  return false;
}

auto Type::has_qualifier(Qualifier qual) const -> bool {
  if (m_mut)
    return (qual == Qualifier::Mut);
  if (const auto* ptr_base = base_dyn_cast<Ptr>(); ptr_base)
    return ptr_base->has_qualifier(qual);
  return false;
}

auto BaseType::apply_generic_substitution(Sub sub) const -> const BaseType* {
  yume_assert(is_generic(), "Can't perform generic substitution without a generic type");
  yume_assert(sub.target != nullptr && sub.replace != nullptr,
              "Can't perform generic substitution without anything to substitute");
  if (this == sub.target)
    return sub.replace.base();

  if (const auto* qual_this = dyn_cast<Qual>(this))
    if (qual_this->m_mut)
      return &mut_base()->apply_generic_substitution(sub)->known_mut();

  if (const auto* ptr_this = dyn_cast<Ptr>(this))
    return &ptr_base()->apply_generic_substitution(sub)->known_qual(ptr_this->qualifier());

  if (const auto* st_this = dyn_cast<Struct>(this)) {
    // Gah!
    auto subs = Substitution{};
    for (const auto& [k, v] : st_this->subs()) {
      subs.try_emplace(k, v.base()->apply_generic_substitution(sub));
    }

    return &st_this->emplace_subbed(move(subs));
  }

  return nullptr;
}

auto Struct::emplace_subbed(Substitution sub) const -> const Struct& {
  if (m_parent != nullptr)
    return m_parent->emplace_subbed(move(sub));

  if (sub == *m_subs)
    return *this;

  auto existing = m_subbed.find(sub);
  if (existing == m_subbed.end()) {
    auto [iter, success] = m_subbed.emplace(move(sub), make_unique<Struct>(base_name(), fields(), nullptr));
    iter->second->m_subs = &iter->first;
    iter->second->m_parent = this;
    return *iter->second;
  }
  return *existing->second;
}

auto BaseType::mut_base() const -> const BaseType* {
  if (const auto* qual = dyn_cast<Qual>(this))
    return &qual->base();
  return nullptr;
}

auto Type::mut_base() const noexcept -> optional<Type> {
  if (m_mut)
    return *this;
  return std::nullopt;
}

auto BaseType::ptr_base() const -> const BaseType* {
  if (const auto* ptr = dyn_cast<Ptr>(this))
    return &ptr->base();
  return nullptr;
}

auto Type::ptr_base() const noexcept -> optional<Type> {
  if (const auto* ptr = base_dyn_cast<Ptr>())
    return &ptr->base();
  return {};
}

auto BaseType::coalesce(const BaseType& other) const -> const BaseType* {
  if (this == &other)
    return this;
  if (is_mut() && mut_base() == &other)
    return this;
  if (other.is_mut() && other.mut_base() == this)
    return &other;

  return nullptr;
}

auto Type::coalesce(Type other) const noexcept -> optional<Type> {
  if (*this == other)
    return *this;
  if (m_mut && !other.m_mut && m_base == other.m_base)
    return *this;
  if (other.m_mut && !m_mut && other.m_base == m_base)
    return other;

  return std::nullopt;
}

auto BaseType::intersect(const BaseType& other) const -> const BaseType* {
  if (this == &other)
    return this;
  if (is_mut() && mut_base() == &other)
    return &other;
  if (other.is_mut() && other.mut_base() == this)
    return this;

  return nullptr;
}

auto Type::intersect(Type other) const noexcept -> optional<Type> {
  if (*this == other)
    return *this;
  if (m_mut && !other.m_mut && m_base == other.m_base)
    return other;
  if (other.m_mut && !m_mut && other.m_base == m_base)
    return *this;

  return std::nullopt;
}

auto BaseType::without_mut() const -> const BaseType& { return is_mut() ? *mut_base() : *this; }
auto Type::without_mut() const noexcept -> Type { return {m_base}; }

auto Type::name() const -> string {
  if (m_mut)
    return m_base->name() + qual_suffix(Qualifier::Mut);
  return m_base->name();
}

auto Qual::name() const -> string {
  if (m_mut)
    return m_base.name() + qual_suffix(Qualifier::Mut);
  return m_base.name();
}
auto Ptr::name() const -> string { return m_base.name() + qual_suffix(m_qual); }
auto Struct::name() const -> string {
  if (m_subs->empty())
    return base_name();

  auto ss = stringstream{};
  ss << base_name() << "{";
  for (const auto& i : llvm::enumerate(*m_subs)) {
    if (i.index() > 0)
      ss << ",";
    ss << i.value().second.name();
  }
  ss << "}";

  return ss.str();
}

namespace detail {
static constexpr size_t BITSIZE_8 = 8;
static constexpr size_t BITSIZE_16 = 16;
static constexpr size_t BITSIZE_32 = 32;
static constexpr size_t BITSIZE_64 = 64;

struct MinMax {
  uint64_t u_min;
  uint64_t u_max;
  int64_t s_min;
  int64_t s_max;
};

template <typename UIntType> consteval auto minmax_for_bits() -> MinMax {
  using SIntType = typename std::make_signed<UIntType>::type;

  return {std::numeric_limits<UIntType>::min(), std::numeric_limits<UIntType>::max(),
          std::numeric_limits<SIntType>::min(), std::numeric_limits<SIntType>::max()};
}

constexpr auto minmax_for_bits(size_t bits) -> MinMax {
  switch (bits) {
  case BITSIZE_8: return minmax_for_bits<uint8_t>();
  case BITSIZE_16: return minmax_for_bits<uint16_t>();
  case BITSIZE_32: return minmax_for_bits<uint32_t>();
  case BITSIZE_64: return minmax_for_bits<uint64_t>();
  default: throw std::logic_error("Integer type must be 8, 16, 32, or 64 bits, not "s + std::to_string(bits));
  };
}
} // namespace detail

auto Int::in_range(int64_t num) const -> bool {
  if (num < 0 && !m_signed)
    return false;
  auto min_max = detail::minmax_for_bits(m_size);
  if (m_signed)
    return num >= min_max.s_min && num <= min_max.s_max;
  return static_cast<uint64_t>(num) >= min_max.u_min && static_cast<uint64_t>(num) <= min_max.u_max;
}
} // namespace yume::ty
