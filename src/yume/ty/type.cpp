#include "type.hpp"
#include "ast/ast.hpp"
#include "compiler/vals.hpp"
#include "qualifier.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include "ty/type_base.hpp"
#include "util.hpp"
#include <cstddef>
#include <limits>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ErrorHandling.h>
#include <map>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace yume::ty {
static auto qual_suffix(Qualifier qual) -> string {
  switch (qual) {
  case Qualifier::Mut: return " mut";
  case Qualifier::Ptr: return " ptr";
  case Qualifier::Ref: return " ref";
  default: return "";
  }
}

auto Type::known_qual(Qualifier qual) const -> Type {
  switch (qual) {
  case Qualifier::Mut: return {m_base, true, false};
  case Qualifier::Ref: return {m_base, false, true};
  case Qualifier::Ptr:
    if (m_base->m_known_ptr == nullptr)
      m_base->m_known_ptr = std::make_unique<Ptr>(m_base->base_name(), *this, qual);

    return {m_base->m_known_ptr.get()};
  case Qualifier::Type:
    if (m_base->m_known_meta == nullptr)
      m_base->m_known_meta = std::make_unique<Meta>(this->base());

    return {m_base->m_known_meta.get()};
  case Qualifier::Opaque:
    if (m_base->m_known_opaque_self == nullptr)
      m_base->m_known_opaque_self = std::make_unique<OpaqueSelf>(this->base());

    return {m_base->m_known_opaque_self.get()};
  }
}

static void visit_subs(Type a, Type b, std::unordered_map<string, ty::Type>& sub) {
  yume_assert(b.is_generic(), "Cannot substitute generics in a non-generic type");

  // `Foo ptr` -> `T ptr`, with `T = Foo`.
  if (auto a_ptr_base = a.ptr_base(), b_ptr_base = b.ptr_base();
      a_ptr_base && b_ptr_base && a.base_cast<Ptr>()->qualifier() == b.base_cast<Ptr>()->qualifier()) {
    return visit_subs(*a_ptr_base, *b_ptr_base, sub);
  }
  // `Foo mut` -> `T mut`, with `T = Foo`.
  if (a.is_mut() && b.is_mut())
    return visit_subs(a.ensure_mut_base(), b.ensure_mut_base(), sub);
  // `Foo type` -> `T type`, with `T = Foo`.
  if (a.is_meta() && b.is_meta())
    return visit_subs(a.without_meta(), b.without_meta(), sub);

  // `Foo ptr mut` -> `T ptr`, with `T = Foo`.
  if (a.is_mut() && !b.is_mut())
    return visit_subs(a.ensure_mut_base(), b, sub);

  // `Foo{Bar}` -> `Foo{T}`, with `T = Foo`.
  if (auto a_st_ty = a.base_dyn_cast<Struct>(), b_st_ty = b.base_dyn_cast<Struct>();
      a_st_ty != nullptr && b_st_ty != nullptr) {
    if (a_st_ty->base_name() == b_st_ty->base_name()) {
      // TODO(rymiel): Currently only handling type parameters
      auto a_ty_mapping = a_st_ty->subs()->type_mappings();
      const auto* b_subs = b_st_ty->subs();
      for (auto [a_key, a_sub] : a_ty_mapping) {
        const auto* b_mapping = b_subs->mapping_ref_or_null({a_key});
        if (b_mapping != nullptr && b_mapping->unassigned())
          sub.try_emplace(a_key, a_sub);
      }
    }
  }

  // Substitution impossible! For example, `Foo` -> `T ptr`.
  if (!b.base_isa<Generic>())
    return;

  // Any other generic that didn't match above.
  // `Foo ptr` -> `T`, with `T = Foo ptr`.
  sub.try_emplace(b.base_cast<Generic>()->name(), a);
}

auto Type::determine_generic_subs(Type generic, const Substitutions& subs) const -> optional<Substitutions> {
  yume_assert(generic.is_generic(), "Cannot substitute generics in a non-generic type");

  auto clean_subs = subs;
  std::unordered_map<string, ty::Type> replacements{};

  visit_subs(*this, generic, replacements);
  for (auto [k, v] : subs.mapping()) {
    if (!k->holds_type())
      continue; // Only determining type arguments

    auto iter = replacements.find(k->name);
    if (iter == replacements.end()) {
      // No new value was found for this key, so leave it as it was
      continue;
    }

    auto new_v = iter->second;

    if (v->unassigned()) {
      // No value existed anyway, so we can put the new value in directly
      clean_subs.associate(*k, new_v);
      continue;
    }

    auto existing_v = v->as_type();
    auto intersection = new_v.intersect(existing_v);

    if (!intersection.has_value()) {
      // The types cannot coexist; this substitution cannot proceed;
      return {};
    }

    clean_subs.associate(*k, *intersection);
  }

  return clean_subs;
}

auto Type::compatibility(Type other, Compat compat) const -> Compat {
  if (*this == other) {
    compat.valid = true;
    return compat;
  }

  // `Foo mut` -> `Foo`.
  // Note that the base types are also compared, so `I32 mut` -> `I64`.
  if (this->is_mut() && other.is_unqualified()) {
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

  // A function type with captures can be converted to a matching function type without captures.
  // A function type without captures can be converted to a matching function *pointer* type.
  if (const auto this_fn = base_dyn_cast<Function>(), other_fn = other.base_dyn_cast<Function>();
      (this_fn != nullptr) && (other_fn != nullptr)) {
    if (this_fn->m_args == other_fn->m_args && this_fn->m_ret == other_fn->m_ret && other_fn->m_closure.empty() &&
        this_fn->m_fn_ptr == other_fn->m_fn_ptr) {
      compat.valid = true;
      return compat;
    }
    if (this_fn->m_args == other_fn->m_args && this_fn->m_ret == other_fn->m_ret && this_fn->m_closure.empty() &&
        !this_fn->m_fn_ptr && other_fn->m_fn_ptr) {
      compat.valid = true;
      compat.conv.kind = Conv::FnPtr;
      return compat;
    }
  }

  const auto* this_st = base_dyn_cast<Struct>();
  const auto* other_st = other.base_dyn_cast<Struct>();
  // A struct type which implements an interface can be casted to said interface.
  if ((this_st != nullptr) && (other_st != nullptr) && (other_st->is_interface()) &&
      (this_st->implements().has_value()) && (this_st->implements()->ensure_ty() == other.m_base) &&
      (this->is_mut() == other.is_mut()) && (this->is_ref() == other.is_ref())) {
    compat.valid = true;
    compat.conv.kind = Conv::Virtual;
    return compat;
  }

  // An interface is essentially always opaque, and thus can be implicitly converted to be "opaque"
  if (const auto* other_opaque = other.base_dyn_cast<OpaqueSelf>();
      (this_st != nullptr) && (other_opaque != nullptr) && (this_st->is_interface()) && (!this->is_opaque_self()) &&
      (this_st == other_opaque->indirect()) && (this->is_mut() == other.is_mut()) &&
      (this->is_ref() == other.is_ref())) {
    // TODO(rymiel): should this recurse?
    compat.valid = true;
    return compat;
  }

  // An opaque struct type converted to a regular struct type is basically just a dereference.
  // TODO(rymiel): This is sorta abusing the notion of "dereference", come up with a separate type? Or merge with
  // Virtual somehow?
  // TODO(rymiel): This doesn't support anything related to mutable types, should it?
  if (const auto* this_opaque = this->base_dyn_cast<OpaqueSelf>();
      (this_opaque != nullptr) && (other_st != nullptr) && (!other_st->is_interface()) && (!other.is_opaque_self()) &&
      (other_st == this_opaque->indirect()) && (this->is_unqualified()) && (other.is_unqualified())) {
    // TODO(rymiel): should this recurse?
    compat.conv.dereference = true;
    compat.valid = true;
    return compat;
  }

  return compat;
}

auto Type::is_generic() const noexcept -> bool {
  if (base_isa<Generic>())
    return true;

  if (base_isa<Ptr>())
    return ensure_ptr_base().is_generic();

  if (base_isa<Meta>())
    return without_meta().is_generic(); // TODO(rymiel): Needs a `ensure_meta_indirect` or something idk

  if (const auto* fn_base = base_dyn_cast<Function>(); fn_base != nullptr) {
    auto ret = fn_base->ret();
    return std::ranges::any_of(fn_base->args(), &Type::is_generic) || (ret.has_value() && ret->is_generic());
  }

  if (const auto* struct_ty = base_dyn_cast<Struct>())
    return !struct_ty->subs()->fully_substituted();

  return false;
}

auto Type::is_slice() const noexcept -> bool {
  if (const auto* base = base_dyn_cast<Struct>())
    return base->base_name() == "Slice" && (base->m_subs != nullptr) && base->m_subs->size() == 1;

  return false;
};

auto Type::is_opaque_self() const noexcept -> bool { return base_isa<OpaqueSelf>(); };

auto Type::is_meta() const noexcept -> bool { return base_isa<Meta>(); };

auto Type::is_trivially_destructible() const -> bool {
  if (base_isa<ty::Int>() || base_isa<ty::Ptr>() || base_isa<ty::Function>() || base_isa<ty::Nil>())
    return true;

  if (is_slice())
    return false;

  if (const auto* struct_type = base_dyn_cast<ty::Struct>()) {
    return std::ranges::all_of(
        struct_type->fields(), [](const auto& i) { return i.is_trivially_destructible(); }, &ast::TypeName::ensure_ty);
  }

  // A generic or something, shouldn't occur
  throw std::logic_error("Cannot check if "s + name() + " is trivially destructible");
}

auto Type::has_qualifier(Qualifier qual) const -> bool {
  if (m_mut)
    return (qual == Qualifier::Mut);
  if (m_ref)
    return (qual == Qualifier::Ref);
  if (const auto* ptr_base = base_dyn_cast<Ptr>(); ptr_base)
    return ptr_base->has_qualifier(qual);
  return false;
}

auto Type::apply_generic_substitution(const Substitutions& sub) const -> optional<Type> {
  yume_assert(is_generic(), "Can't perform generic substitution without a generic type");

  if (const auto* generic_this = base_dyn_cast<Generic>()) {
    if (auto mapped = sub.find_type(generic_this->name()); mapped.has_value())
      return Type{mapped->base(), mapped->is_mut() || m_mut, mapped->is_ref() || m_ref};
  }

  if (const auto* ptr_this = base_dyn_cast<Ptr>()) {
    auto base = ensure_ptr_base().apply_generic_substitution(sub);
    if (!base.has_value())
      return {};
    return base->known_qual(ptr_this->qualifier());
  }

  if (is_meta()) {
    auto base = without_meta().apply_generic_substitution(sub);
    if (!base.has_value())
      return {};
    return base->known_meta();
  }

  if (const auto* st_this = base_dyn_cast<Struct>())
    return Type{&st_this->apply_substitutions(sub), m_mut, m_ref};

  return {};
}

auto Struct::apply_substitutions(Substitutions sub) const -> const Struct& {
  if (m_parent != nullptr)
    return m_parent->apply_substitutions(move(sub));

  if (m_subs != nullptr && sub == *m_subs)
    return *this;

  auto existing = m_subbed.find(sub);
  if (existing == m_subbed.end()) {
    auto [iter, success] = m_subbed.emplace(move(sub), make_unique<Struct>(base_name(), fields(), m_decl, nullptr));
    iter->second->m_subs = &iter->first;
    iter->second->m_parent = this;
    return *iter->second;
  }
  return *existing->second;
}

auto Struct::is_interface() const -> bool { return m_decl->st_ast.is_interface; }

auto Struct::implements() const -> const ast::OptionalType& { return m_decl->st_ast.implements; }

auto Type::mut_base() const noexcept -> optional<Type> {
  if (is_mut())
    return Type(m_base);
  return std::nullopt;
}

auto Type::ensure_mut_base() const -> Type {
  if (is_mut())
    return {m_base};
  throw std::logic_error("Tried calling ensure_mut_base on a type that isn't a mutable reference");
}

auto Type::ptr_base() const noexcept -> optional<Type> {
  if (const auto* ptr = base_dyn_cast<Ptr>())
    return ptr->pointee();
  return {};
}

auto Type::ensure_ptr_base() const -> Type {
  if (const auto* ptr = base_dyn_cast<Ptr>())
    return ptr->pointee();
  throw std::logic_error("Tried calling ensure_ptr_base on a type that isn't a pointer-like type");
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

auto Type::without_opaque() const noexcept -> Type {
  if (const auto* opaque_self = base_dyn_cast<OpaqueSelf>(); opaque_self != nullptr)
    return {opaque_self->indirect()};
  return *this;
}

auto Type::without_meta() const noexcept -> Type {
  if (const auto* meta = base_dyn_cast<Meta>(); meta != nullptr)
    return {meta->indirect()};
  return *this;
}

auto Type::generic_base() const noexcept -> Type {
  if (const auto* st = base_dyn_cast<Struct>(); st != nullptr) {
    auto primary_generic = st->decl()->get_self_ty();
    yume_assert(primary_generic.has_value(), "Generic type doesn't have a known primary unsubstituted type");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): clang-tidy doesn't accept yume_assert as an assertion
    return {*primary_generic};
  }
  return *this;
}

auto Type::name() const -> string {
  auto name = m_base->name();
  if (m_mut)
    name += qual_suffix(Qualifier::Mut);
  if (m_ref)
    name += qual_suffix(Qualifier::Ref);
  return name;
}
auto Type::base_name() const -> string { return m_base->name(); }

auto Ptr::name() const -> string { return m_base.name() + qual_suffix(m_qual); }

auto Struct::name() const -> string {
  if (m_subs == nullptr || m_subs->empty())
    return base_name();

  auto ss = stringstream{};
  ss << base_name() << "{";
  for (const auto& i : llvm::enumerate(m_subs->mapping())) {
    auto [key, mapping] = i.value();
    if (i.index() > 0)
      ss << ",";
    ss << (mapping->unassigned() ? key->name : mapping->name());
  }
  ss << "}";

  return ss.str();
}

auto Function::name() const -> string {
  auto ss = stringstream{};
  ss << "(" << base_name();
  if (!m_closure.empty()) {
    ss << "[";
    for (const auto& i : llvm::enumerate(m_closure)) {
      if (i.index() > 0)
        ss << ",";
      ss << i.value().name();
    }
    ss << "] ";
  }
  for (const auto& i : llvm::enumerate(m_args)) {
    if (i.index() > 0)
      ss << ",";
    ss << i.value().name();
  }
  ss << "->";
  if (m_fn_ptr)
    ss << "ptr";
  if (m_ret.has_value())
    ss << m_ret->name();
  ss << ")";

  return ss.str();
}

auto Type::opaque_equal(const Type& other) const noexcept -> bool {
  return *this == other || (this->is_opaque_self() && other.is_opaque_self());
};

namespace detail {
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
  case 8: return minmax_for_bits<uint8_t>();
  case 16: return minmax_for_bits<uint16_t>();
  case 32: return minmax_for_bits<uint32_t>();
  case 64: return minmax_for_bits<uint64_t>();
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
