#pragma once

#include "ast/ast.hpp"
#include "ty/type_base.hpp"
#include "util.hpp"
#include <concepts>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <variant>

namespace yume {
namespace ty {
class Generic;
} // namespace ty

using generic_type_replacements_t = std::unordered_map<string, ty::Type>;
struct GenericTypeReplacements : generic_type_replacements_t {
  auto operator==(const GenericTypeReplacements& other) const noexcept -> bool = default;
};

struct GenericValueKey {
  string name;
  nonnull<ast::Type*> type;
  // std::vector<unique_ptr<ast::Expr>> exprs{};

  auto operator==(const GenericValueKey& other) const noexcept -> bool = default;
  auto operator<=>(const GenericValueKey& other) const noexcept = default;
};
using GenericTypeKey = std::string;

using generic_key_t = visitable_variant<GenericTypeKey, GenericValueKey>;
struct GenericKey : generic_key_t {
  using generic_key_t::visitable_variant;

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] auto holds_type() const -> bool { return std::holds_alternative<GenericTypeKey>(*this); }
  [[nodiscard]] auto holds_value() const -> bool { return std::holds_alternative<GenericValueKey>(*this); }

  [[nodiscard]] auto as_type() const& -> const GenericTypeKey& { return std::get<GenericTypeKey>(*this); }
  [[nodiscard]] auto as_value() const& -> const GenericValueKey& { return std::get<GenericValueKey>(*this); }
};

using Generics = std::vector<GenericKey>;
static_assert(std::copy_constructible<Generics>,
              "Generics should be copy-constructible for avoiding lifetime problems");

using GenericTypeMapping = ty::Type;
using GenericValueMapping = ast::Expr*;

using generic_mapping_t = visitable_variant<std::monostate, GenericTypeMapping, GenericValueMapping>;
struct GenericMapping : generic_mapping_t {
  using generic_mapping_t::visitable_variant;

  [[nodiscard]] auto unassigned() const -> bool { return std::holds_alternative<std::monostate>(*this); }

  [[nodiscard]] auto unsubstituted_primary() const -> bool {
    return visit([](ty::Type type) { return type.is_generic(); }, [](auto /* else */) { return false; });
  }

  [[nodiscard]] auto unassigned_or_unsubstituted() const -> bool { return unassigned() || unsubstituted_primary(); }

  [[nodiscard]] auto name() const -> string {
    return visit([](std::monostate /* unassigned */) { return "?"s; }, //
                 [](ty::Type type) { return type.name(); },            //
                 [](const ast::Expr* expr) { return expr->describe(); });
  }

  [[nodiscard]] auto holds_type() const -> bool { return std::holds_alternative<GenericTypeMapping>(*this); }
  [[nodiscard]] auto holds_value() const -> bool { return std::holds_alternative<GenericValueMapping>(*this); }

  [[nodiscard]] auto as_type() const& -> const GenericTypeMapping& { return std::get<GenericTypeMapping>(*this); }
  [[nodiscard]] auto as_value() const& -> const GenericValueMapping& { return std::get<GenericValueMapping>(*this); }
};

struct Substitutions {
private:
  vector<GenericKey> m_keys;
  vector<GenericMapping> m_mapping;
  nullable<Substitutions*> m_parent = nullptr;
  vector<ty::Generic*> m_generic_type_fallbacks;

public:
  Substitutions() = delete;
  [[deprecated]] Substitutions(vector<GenericKey> keys) : m_keys{move(keys)} {
    for ([[maybe_unused]] const auto& i : m_keys)
      m_mapping.emplace_back(std::monostate{});
  }
  Substitutions(vector<GenericKey> keys, const vector<unique_ptr<ty::Generic>>& generic_type_fallbacks,
                nullable<Substitutions*> parent = nullptr)
      : m_keys{move(keys)}, m_parent{parent} {
    for (const auto& i : generic_type_fallbacks)
      m_generic_type_fallbacks.emplace_back(i.get());
    for ([[maybe_unused]] const auto& i : m_keys)
      m_mapping.emplace_back(std::monostate{});
  }
  Substitutions(vector<GenericKey> keys, vector<ty::Generic*> generic_type_fallbacks,
                nullable<Substitutions*> parent = nullptr)
      : m_keys{move(keys)}, m_parent{parent}, m_generic_type_fallbacks{move(generic_type_fallbacks)} {
    for ([[maybe_unused]] const auto& i : m_keys)
      m_mapping.emplace_back(std::monostate{});
  }
  Substitutions(nonnull<Substitutions*> parent) : m_parent{parent} {}

  [[deprecated]] void types() const = delete;
  [[deprecated]] void values() const = delete;
  [[nodiscard]] auto empty() const -> bool { return m_mapping.empty() && (m_parent == nullptr || m_parent->empty()); }
  [[nodiscard]] auto size() const -> size_t { return m_mapping.size() + (m_parent == nullptr ? 0 : m_parent->size()); }
  [[nodiscard]] auto fully_substituted() const -> bool {
    // We consider types with no generic arguments at all to be fully substituted
    return (empty() || std::ranges::none_of(m_mapping, &GenericMapping::unassigned_or_unsubstituted)) &&
           ((m_parent == nullptr) || m_parent->fully_substituted());
  }

  [[nodiscard]] auto mapping_ref_or_null(string_view generic) -> nullable<GenericMapping*>;
  [[nodiscard]] auto mapping_ref_or_null(string_view generic) const -> nullable<const GenericMapping*>;
  [[nodiscard]] auto mapping_ref_or_null(const GenericValueKey& generic) -> nullable<GenericMapping*>;
  [[nodiscard]] auto mapping_ref_or_null(const GenericValueKey& generic) const -> nullable<const GenericMapping*>;
  [[nodiscard]] auto mapping_ref_or_null_direct(GenericKey generic) -> nullable<GenericMapping*>;
  [[nodiscard]] auto mapping_ref_or_null_direct(GenericKey generic) const -> nullable<const GenericMapping*>;
  [[nodiscard]] auto mapping_ref(string_view generic) -> GenericMapping& {
    auto* ptr = mapping_ref_or_null(generic);
    yume_assert(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };
  [[nodiscard]] auto mapping_ref(string_view generic) const -> const GenericMapping& {
    const auto* ptr = mapping_ref_or_null(generic);
    yume_assert(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };
  [[nodiscard]] auto mapping_ref(const GenericValueKey& generic) -> GenericMapping& {
    auto* ptr = mapping_ref_or_null(generic);
    yume_assert(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };
  [[nodiscard]] auto mapping_ref(const GenericValueKey& generic) const -> const GenericMapping& {
    const auto* ptr = mapping_ref_or_null(generic);
    yume_assert(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };
  [[nodiscard]] auto mapping_ref_direct(GenericKey generic) -> GenericMapping& {
    auto* ptr = mapping_ref_or_null_direct(move(generic));
    yume_assert(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };
  [[nodiscard]] auto mapping_ref_direct(GenericKey generic) const -> const GenericMapping& {
    const auto* ptr = mapping_ref_or_null_direct(move(generic));
    yume_assert(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };

  [[nodiscard]] auto find_type(string_view generic_name) const -> optional<ty::Type> {
    const auto* mapping = mapping_ref_or_null(generic_name);

    if (mapping == nullptr)
      return std::nullopt;

    if (mapping->unassigned())
      return std::nullopt;

    yume_assert(std::holds_alternative<GenericTypeMapping>(*mapping),
                "A generic type key must correspond to a generic type mapping");

    return std::get<GenericTypeMapping>(*mapping);
  }

  [[nodiscard]] auto type_mappings() const -> std::map<string, ty::Type>;

  [[deprecated]] void add(const string& key, ty::Type type) = delete;

  [[deprecated]] void add(const string& key, ast::Expr* expr) = delete;

  void associate(string_view key, GenericTypeMapping mapping) { mapping_ref(key) = mapping; }
  void associate(const GenericValueKey& key, GenericValueMapping mapping) { mapping_ref(key) = mapping; }
  void associate_direct(GenericKey key, GenericMapping mapping) { mapping_ref_direct(move(key)) = mapping; }

  auto append_new_association(GenericKey key) -> GenericMapping& {
    m_keys.emplace_back(move(key));
    return m_mapping.emplace_back(std::monostate{});
  }

  [[nodiscard, deprecated]] auto all_ordered() const -> vector<std::pair<string, GenericMapping>> {
    vector<std::pair<string, GenericMapping>> vec{};
    if (m_parent != nullptr)
      vec = m_parent->all_ordered(); // TODO(rymiel): wrong

    for (const auto& [k, v] : llvm::zip(m_keys, m_mapping))
      vec.emplace_back(k.name(), v);

    return vec;
  }
  void dump(llvm::raw_ostream& os) const {
    int i = 0;
    for (const auto& [k, v] : llvm::zip(m_keys, m_mapping)) {
      if (i++ > 0)
        os << ", ";
      os << k.name() << "=" << v.name();
    }
    if (m_parent != nullptr) {
      os << "; ";
      m_parent->dump(os);
    }
  }

  [[nodiscard, deprecated]] auto generics() const -> const Generics* { return &m_keys; }

  [[nodiscard]] auto all_keys() const -> vector<const GenericKey*> {
    vector<const GenericKey*> keys{};
    keys.reserve(m_keys.size());
    for (const auto& k : m_keys)
      keys.push_back(&k);
    if (m_parent != nullptr) {
      auto parent_keys = m_parent->all_keys();
      keys.insert(keys.end(), parent_keys.begin(), parent_keys.end());
    }

    return keys;
  }
  [[nodiscard]] auto all_mappings() const -> vector<const GenericMapping*> {
    vector<const GenericMapping*> mappings{};
    mappings.reserve(m_mapping.size());
    for (const auto& m : m_mapping)
      mappings.push_back(&m);
    if (m_parent != nullptr) {
      auto parent_mappings = m_parent->all_mappings();
      mappings.insert(mappings.end(), parent_mappings.begin(), parent_mappings.end());
    }

    return mappings;
  }
  [[nodiscard]] auto all_mappings() -> vector<GenericMapping*> {
    vector<GenericMapping*> mappings{};
    mappings.reserve(m_mapping.size());
    for (auto& m : m_mapping)
      mappings.push_back(&m);
    if (m_parent != nullptr) {
      auto parent_mappings = m_parent->all_mappings();
      mappings.insert(mappings.end(), parent_mappings.begin(), parent_mappings.end());
    }

    return mappings;
  }

  [[nodiscard]] auto mapping() { return llvm::zip(all_keys(), all_mappings()); }
  [[nodiscard]] auto mapping() const { return llvm::zip(all_keys(), all_mappings()); }

  [[nodiscard]] auto get_generic_fallback(string_view generic_name) const -> ty::Generic*;

  [[nodiscard]] auto deep_copy() const -> Substitutions;

  auto operator==(const Substitutions& other) const noexcept -> bool = default;
  auto operator<=>(const Substitutions& other) const noexcept = default;
};
} // namespace yume
