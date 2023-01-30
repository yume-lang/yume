#include "ty/substitution.hpp"
#include "ty/type.hpp"
#include <stdexcept>
#include <variant>

namespace yume {
auto Substitutions::mapping_ref_or_null(const GenericKey& generic) -> nullable<GenericValue*> {
  auto iter = std::ranges::find(m_keys, generic);

  if (iter == m_keys.end())
    return nullptr;

  return &m_values.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::mapping_ref_or_null(const GenericKey& generic) const -> nullable<const GenericValue*> {
  auto iter = std::ranges::find(m_keys, generic);

  if (iter == m_keys.end())
    return nullptr;

  return &m_values.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::type_mappings() const -> std::map<string, ty::Type> {
  auto mapping = std::map<string, ty::Type>{};

  for (const auto& [k, v] : llvm::zip(m_keys, m_values))
    if (k.holds_type() && !v.unassigned())
      mapping.insert_or_assign(k.name, v.as_type());

  return mapping;
}
auto Substitutions::get_generic_fallback(string_view generic_name) const -> ty::Generic* {
  auto iter = std::ranges::find(m_generic_type_fallbacks, generic_name, &ty::Generic::name);

  if (iter == m_generic_type_fallbacks.end())
    return nullptr;

  return *iter;
}
auto Substitutions::deep_copy() const -> Substitutions {
  vector<GenericKey> keys = {};
  for (const auto* i : all_keys())
    keys.emplace_back(*i);
  Substitutions copy{move(keys), m_generic_type_fallbacks, nullptr};
  for (const auto [k, v] : mapping())
    copy.associate(*k, *v);

  return copy;
}
} // namespace yume
