#pragma once

#include "ast/ast.hpp"
#include "ty/type_base.hpp"
#include "util.hpp"
#include <concepts>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <stdexcept>
#include <variant>

namespace yume {
namespace ty {
class Generic;
} // namespace ty

struct GenericKey {
  string name{};
  nullable<ast::Type*> expr_type{};
  // std::vector<unique_ptr<ast::Expr>> exprs{};

  /* implicit */ GenericKey(string_view name) : name{name} {}
  GenericKey(string_view name, nonnull<ast::Type*> type) : name{name}, expr_type{type} {}

  [[nodiscard]] auto holds_type() const -> bool { return expr_type == nullptr; }
  [[nodiscard]] auto holds_expr() const -> bool { return expr_type != nullptr; }

  auto operator==(const GenericKey& other) const noexcept -> bool = default;
  auto operator<=>(const GenericKey& other) const noexcept = default;
};

struct GenericValue {
  optional<ty::Type> type{};
  nullable<ast::Expr*> expr{};

  GenericValue() = default;
  GenericValue(ty::Type type) : type{type} {}
  GenericValue(nonnull<ast::Expr*> expr) : expr{expr} {}

  [[nodiscard]] auto unassigned() const -> bool { return expr == nullptr && !type.has_value(); }
  [[nodiscard]] auto holds_type() const -> bool { return type.has_value(); }
  [[nodiscard]] auto holds_expr() const -> bool { return expr != nullptr; }

  [[nodiscard]] auto as_type() const& -> const ty::Type& {
    YUME_ASSERT(type.has_value(), "Generic value does not hold a type");
    return *type;
  }
  [[nodiscard]] auto as_expr() const& -> const ast::Expr* { return expr; }

  [[nodiscard]] auto unsubstituted_primary() const -> bool { return type.has_value() && type->is_generic(); }

  [[nodiscard]] auto unassigned_or_unsubstituted() const -> bool { return unassigned() || unsubstituted_primary(); }

  [[nodiscard]] auto name() const -> string {
    if (unassigned())
      return "?"s;
    if (type.has_value())
      return type->name();
    return expr->describe();
  }

  auto operator==(const GenericValue& other) const noexcept -> bool = default;
};

struct Substitutions {
private:
  vector<GenericKey> m_keys;
  vector<GenericValue> m_values;
  vector<ty::Generic*> m_generic_type_fallbacks;

public:
  Substitutions() = delete;
  Substitutions(vector<GenericKey> keys, const vector<unique_ptr<ty::Generic>>& generic_type_fallbacks,
                nullable<Substitutions*> parent = nullptr)
      : m_keys{move(keys)} {
    for (const auto& i : generic_type_fallbacks)
      m_generic_type_fallbacks.emplace_back(i.get());
    for ([[maybe_unused]] const auto& i : m_keys)
      m_values.emplace_back();

    if (parent != nullptr) {
      for (auto [k, v] : llvm::zip(parent->m_keys, parent->m_values)) {
        m_keys.push_back(k);
        m_values.push_back(v);
      }
      for (auto* f : parent->m_generic_type_fallbacks)
        m_generic_type_fallbacks.push_back(f);
    }
  }
  Substitutions(vector<GenericKey> keys, vector<ty::Generic*> generic_type_fallbacks,
                nullable<Substitutions*> parent = nullptr)
      : m_keys{move(keys)}, m_generic_type_fallbacks{move(generic_type_fallbacks)} {
    for ([[maybe_unused]] const auto& i : m_keys)
      m_values.emplace_back();

    if (parent != nullptr) {
      for (auto [k, v] : llvm::zip(parent->m_keys, parent->m_values)) {
        m_keys.push_back(k);
        m_values.push_back(v);
      }
      for (auto* f : parent->m_generic_type_fallbacks)
        m_generic_type_fallbacks.push_back(f);
    }
  }

  [[nodiscard]] auto empty() const -> bool { return m_values.empty(); }
  [[nodiscard]] auto size() const -> size_t { return m_values.size(); }
  [[nodiscard]] auto fully_substituted() const -> bool {
    // We consider types with no generic arguments at all to be fully substituted
    return (empty() || std::ranges::none_of(m_values, &GenericValue::unassigned_or_unsubstituted));
  }

  [[nodiscard]] auto mapping_ref_or_null(const GenericKey& generic) -> nullable<GenericValue*>;
  [[nodiscard]] auto mapping_ref_or_null(const GenericKey& generic) const -> nullable<const GenericValue*>;
  [[nodiscard]] auto mapping_ref(const GenericKey& generic) -> GenericValue& {
    auto* ptr = mapping_ref_or_null(generic);
    YUME_ASSERT(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };
  [[nodiscard]] auto mapping_ref(const GenericKey& generic) const -> const GenericValue& {
    const auto* ptr = mapping_ref_or_null(generic);
    YUME_ASSERT(ptr != nullptr, "Mapped value must not be null");
    return *ptr;
  };

  [[nodiscard]] auto find_type(string_view generic_name) const -> optional<ty::Type> {
    const auto* mapping = mapping_ref_or_null(generic_name);

    if (mapping == nullptr)
      return std::nullopt;

    if (mapping->unassigned())
      return std::nullopt;

    YUME_ASSERT(mapping->holds_type(), "A generic type key must correspond to a generic type mapping");

    return mapping->as_type();
  }

  [[nodiscard]] auto type_mappings() const -> std::map<string, ty::Type>;

  void associate(const GenericKey& key, GenericValue value) { mapping_ref(key) = value; }

  auto append_new_association(GenericKey key) -> GenericValue& {
    m_keys.emplace_back(move(key));
    return m_values.emplace_back();
  }

  void dump(llvm::raw_ostream& os) const {
    int i = 0;
    for (const auto& [k, v] : llvm::zip(m_keys, m_values)) {
      if (i++ > 0)
        os << ", ";
      os << k.name << "=" << v.name();
    }
  }

  [[nodiscard]] auto all_keys() const -> vector<const GenericKey*> {
    vector<const GenericKey*> keys{};
    keys.reserve(m_keys.size());
    for (const auto& k : m_keys)
      keys.push_back(&k);

    return keys;
  }
  [[nodiscard]] auto all_values() const -> vector<const GenericValue*> {
    vector<const GenericValue*> values{};
    values.reserve(m_values.size());
    for (const auto& m : m_values)
      values.push_back(&m);

    return values;
  }
  [[nodiscard]] auto all_values() -> vector<GenericValue*> {
    vector<GenericValue*> values{};
    values.reserve(m_values.size());
    for (auto& m : m_values)
      values.push_back(&m);

    return values;
  }

  [[nodiscard]] auto mapping() { return llvm::zip(all_keys(), all_values()); }
  [[nodiscard]] auto mapping() const { return llvm::zip(all_keys(), all_values()); }

  [[nodiscard]] auto get_generic_fallback(string_view generic_name) const -> ty::Generic*;

  auto operator==(const Substitutions& other) const noexcept -> bool {
    auto this_keys = this->all_keys();
    auto other_keys = other.all_keys();
    auto this_vals = this->all_values();
    auto other_vals = other.all_values();

    return std::ranges::equal(this_keys, other_keys, [](auto* a, auto* b) { return *a == *b; }) &&
           std::ranges::equal(this_vals, other_vals, [](auto* a, auto* b) { return *a == *b; });
  }
};
} // namespace yume

template <> struct std::hash<yume::Substitutions> {
  auto operator()(const yume::Substitutions& s) const noexcept -> std::size_t {
    uint64_t seed = 0;
    for (const auto* k : s.all_keys()) {
      yume::hash_combine(seed, k->name);
      yume::hash_combine(seed, k->expr_type);
    }
    for (const auto* v : s.all_values()) {
      yume::hash_combine(seed, v->type.has_value());
      if (v->type.has_value()) {
        yume::hash_combine(seed, v->type->base());
        yume::hash_combine(seed, v->type->is_mut());
        yume::hash_combine(seed, v->type->is_ref());
      }
      yume::hash_combine(seed, v->expr);
    }

    return seed;
  }
};
