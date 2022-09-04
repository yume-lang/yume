#include "type.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include <cstddef>
#include <limits>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ErrorHandling.h>
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
  default: return "";
  }
}

auto Type::known_qual(Qualifier qual) const -> Type {
  if (qual == Qualifier::Mut)
    return {m_base, true};

  const int qual_idx = static_cast<int>(qual);
  const auto& existing = m_base->m_known_ptr_like.at(qual_idx);
  if (existing != nullptr)
    return {existing.get()};

  auto new_qual_type = std::make_unique<Ptr>(m_base->base_name(), *this, qual);
  m_base->m_known_ptr_like.at(qual_idx) = move(new_qual_type);
  return {m_base->m_known_ptr_like.at(qual_idx).get()};
}

static auto visit_subs(Type a, Type b, optional<Sub> sub) -> optional<Sub> {
  yume_assert(b.is_generic(), "Cannot substitute generics in a non-generic type");

  // `Foo ptr` -> `T ptr`, with `T = Foo`.
  if (auto a_ptr_base = a.ptr_base(), b_ptr_base = b.ptr_base();
      a_ptr_base && b_ptr_base && a.base_cast<Ptr>()->qualifier() == b.base_cast<Ptr>()->qualifier()) {
    return visit_subs(*a_ptr_base, *b_ptr_base, sub);
  }
  // `Foo mut` -> `T mut`, with `T = Foo`.
  if (a.is_mut() && b.is_mut())
    return visit_subs(a.ensure_mut_base(), b.without_mut(), sub);

  // `Foo ptr mut` -> `T ptr`, with `T = Foo`.
  if (a.is_mut() && !b.is_mut())
    return visit_subs(a.ensure_mut_base(), b, sub);

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
  return Sub{b.base_cast<Generic>(), a};
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

auto Type::determine_generic_subs(Type generic, Substitution& subs) const -> optional<Sub> {
  yume_assert(generic.is_generic(), "Cannot substitute generics in a non-generic type");

  optional<Sub> sub{};
  sub = visit_subs(*this, generic, sub);
  if (sub)
    if (auto intersection = intersect_generics(subs, sub->target, sub->replace))
      sub->replace = *intersection;

  return sub;
}

auto Type::compatibility(Type other, Compat compat) const -> Compat {
  if (*this == other) {
    compat.valid = true;
    return compat;
  }

  // `Foo mut` -> `Foo`.
  // Note that the base types are also compared, so `I32 mut` -> `I64`.
  if (this->is_mut() && !other.is_mut()) {
    compat.conv.dereference = true;
    compat = ensure_mut_base().compatibility(other, compat);
    return compat;
  }

  // `I32` -> `I64`. `U8` -> `I16`. An implicit integer cast with no loss of information.
  if (const auto this_int = base_dyn_cast<Int>(), other_int = other.base_dyn_cast<Int>();
      (this_int != nullptr) && (other_int != nullptr)) {
    if (this_int->is_signed() == other_int->is_signed() && this_int->size() == other_int->size()) {
      // The two integer types are perfect matches, but werent caught by the pointer equality check above. This is the
      // case for conversions such as `USize` -> `U32` (on a 32-bit platform).
      compat.valid = true;
      return compat;
    }

    if ((this_int->is_signed() == other_int->is_signed() && this_int->size() <= other_int->size()) ||
        (!this_int->is_signed() && other_int->is_signed() && this_int->size() * 2 <= other_int->size())) {
      compat.valid = true;
      compat.conv.kind = Conv::Int;
      return compat;
    }
  }

  // A function type with captures can be converted to a matching function type without captures
  if (const auto this_fn = base_dyn_cast<Function>(), other_fn = other.base_dyn_cast<Function>();
      (this_fn != nullptr) && (other_fn != nullptr)) {
    if (this_fn->m_args == other_fn->m_args && this_fn->m_ret == other_fn->m_ret && other_fn->m_closure.empty()) {
      compat.valid = true;
      return compat;
    }
  }

  return compat;
}

auto Type::is_generic() const noexcept -> bool {
  if (base_isa<Generic>())
    return true;

  if (base_isa<Ptr>())
    return ensure_ptr_base().is_generic();

  if (const auto* struct_ty = base_dyn_cast<Struct>())
    return std::ranges::any_of(struct_ty->subs(), [](const auto& sub) noexcept { return sub.second.is_generic(); });
  // XXX: The above ranges call is repeated often

  return false;
}

auto Type::is_slice() const noexcept -> bool {
  if (const auto* base = base_dyn_cast<Struct>())
    return base->base_name() == "Slice" && (base->m_subs != nullptr) && base->m_subs->size() == 1;

  return false;
};

auto Type::has_qualifier(Qualifier qual) const -> bool {
  if (m_mut)
    return (qual == Qualifier::Mut);
  if (const auto* ptr_base = base_dyn_cast<Ptr>(); ptr_base)
    return ptr_base->has_qualifier(qual);
  return false;
}

auto Type::apply_generic_substitution(Sub sub) const -> optional<Type> {
  yume_assert(is_generic(), "Can't perform generic substitution without a generic type");
  if (m_base == sub.target)
    return Type{sub.replace.base(), m_mut};

  if (const auto* ptr_this = base_dyn_cast<Ptr>())
    return ensure_ptr_base().apply_generic_substitution(sub)->known_qual(ptr_this->qualifier());

  if (const auto* st_this = base_dyn_cast<Struct>()) {
    // Gah!
    auto subs = Substitution{};
    for (const auto& [k, v] : st_this->subs()) {
      subs.try_emplace(k, v.apply_generic_substitution(sub).value());
    }

    return &st_this->emplace_subbed(move(subs));
  }

  return {};
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

auto Type::mut_base() const noexcept -> optional<Type> {
  if (m_mut)
    return Type(m_base);
  return std::nullopt;
}

auto Type::ensure_mut_base() const -> Type {
  if (m_mut)
    return {m_base};
  llvm_unreachable("Tried calling ensure_mut_base on a type that isn't a mutable reference");
}

auto Type::ptr_base() const noexcept -> optional<Type> {
  if (const auto* ptr = base_dyn_cast<Ptr>())
    return ptr->pointee();
  return {};
}

auto Type::ensure_ptr_base() const -> Type {
  if (const auto* ptr = base_dyn_cast<Ptr>())
    return ptr->pointee();
  llvm_unreachable("Tried calling ensure_ptr_base on a type that isn't a pointer-like type");
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

auto Type::intersect(Type other) const noexcept -> optional<Type> {
  if (*this == other)
    return *this;
  if (m_mut && !other.m_mut && m_base == other.m_base)
    return other;
  if (other.m_mut && !m_mut && other.m_base == m_base)
    return *this;

  return std::nullopt;
}

auto Type::without_mut() const noexcept -> Type { return {m_base}; }

auto Type::name() const -> string {
  if (m_mut)
    return m_base->name() + qual_suffix(Qualifier::Mut);
  return m_base->name();
}
auto Type::base_name() const -> string { return m_base->name(); }

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

auto Function::name() const -> string {
  auto ss = stringstream{};
  ss << "->" << base_name();
  if (!m_closure.empty()) {
    ss << "[";
    for (const auto& i : llvm::enumerate(m_closure)) {
      if (i.index() > 0)
        ss << ",";
      ss << i.value().name();
    }
    ss << "]";
  }
  ss << "(";
  for (const auto& i : llvm::enumerate(m_args)) {
    if (i.index() > 0)
      ss << ",";
    ss << i.value().name();
  }
  ss << ")";
  if (m_ret.has_value())
    ss << m_ret->name();

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
