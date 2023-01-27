#include "ty/substitution.hpp"
#include "ty/type.hpp"
#include <stdexcept>
#include <variant>

namespace yume {
auto GenericKey::name() const -> string {
  return visit([](const GenericTypeKey& type) { return type; }, [](const GenericValueKey& expr) { return expr.name; });
}

auto find_by_type(string_view generic) {
  return [generic](const GenericKey& key) {
    return key.visit([generic](const GenericTypeKey& type) { return type == generic; },
                     [](const GenericValueKey& /* value */) { return false; });
  };
}

auto find_by_value(const GenericValueKey& generic) {
  return [&generic](const GenericKey& key) {
    return key.visit([](const GenericTypeKey& /* type */) { return false; },
                     [&generic](const GenericValueKey& value) { return value == generic; });
  };
}

auto Substitutions::mapping_ref_or_null(string_view generic) -> nullable<GenericMapping*> {
  auto iter = std::ranges::find_if(m_keys, find_by_type(generic));

  if (iter == m_keys.end()) {
    if (m_parent != nullptr)
      return m_parent->mapping_ref_or_null(generic);
    return nullptr;
  }

  return &m_mapping.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::mapping_ref_or_null(string_view generic) const -> nullable<const GenericMapping*> {
  auto iter = std::ranges::find_if(m_keys, find_by_type(generic));

  if (iter == m_keys.end()) {
    if (m_parent != nullptr)
      return m_parent->mapping_ref_or_null(generic);
    return nullptr;
  }

  return &m_mapping.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::mapping_ref_or_null(const GenericValueKey& generic) -> nullable<GenericMapping*> {
  auto iter = std::ranges::find_if(m_keys, find_by_value(generic));

  if (iter == m_keys.end()) {
    if (m_parent != nullptr)
      return m_parent->mapping_ref_or_null(generic);
    return nullptr;
  }

  return &m_mapping.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::mapping_ref_or_null(const GenericValueKey& generic) const -> nullable<const GenericMapping*> {
  auto iter = std::ranges::find_if(m_keys, find_by_value(generic));

  if (iter == m_keys.end()) {
    if (m_parent != nullptr)
      return m_parent->mapping_ref_or_null(generic);
    return nullptr;
  }

  return &m_mapping.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::mapping_ref_or_null_direct(GenericKey generic) -> nullable<GenericMapping*> {
  auto iter = std::ranges::find(m_keys, generic);

  if (iter == m_keys.end()) {
    if (m_parent != nullptr)
      return m_parent->mapping_ref_or_null_direct(move(generic));
    return nullptr;
  }

  return &m_mapping.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::mapping_ref_or_null_direct(GenericKey generic) const -> nullable<const GenericMapping*> {
  auto iter = std::ranges::find(m_keys, generic);

  if (iter == m_keys.end()) {
    if (m_parent != nullptr)
      return m_parent->mapping_ref_or_null_direct(move(generic));
    return nullptr;
  }

  return &m_mapping.at(std::distance(m_keys.begin(), iter));
}

auto Substitutions::type_mappings() const -> std::map<string, ty::Type> {
  auto mapping = std::map<string, ty::Type>{};

  if (m_parent != nullptr)
    mapping = m_parent->type_mappings(); // TODO(rymiel): wrong

  for (const auto& [k, v] : llvm::zip(m_keys, m_mapping))
    if (k.holds_type() && !v.unassigned())
      mapping.insert_or_assign(std::get<GenericTypeKey>(k), std::get<GenericTypeMapping>(v));

  return mapping;
}
auto Substitutions::get_generic_fallback(string_view generic_name) const -> ty::Generic* {
  auto iter = std::ranges::find(m_generic_type_fallbacks, generic_name, &ty::Generic::name);

  if (iter == m_generic_type_fallbacks.end()) {
    if (m_parent != nullptr)
      return m_parent->get_generic_fallback(generic_name);
    return nullptr;
  }

  return *iter;
}
auto Substitutions::deep_copy() const -> Substitutions {
  vector<GenericKey> keys = {};
  for (const auto* i : all_keys())
    keys.emplace_back(*i);
  Substitutions copy{move(keys), m_generic_type_fallbacks, nullptr};
  for (const auto [k, v] : mapping())
    copy.associate_direct(*k, *v);

  return copy;
}
} // namespace yume
