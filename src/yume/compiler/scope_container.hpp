#pragma once

#include "util.hpp"
#include <iterator>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <ranges>
#include <vector>

namespace yume {
template <typename T> class ScopeContainerGuard;

template <typename T> class ScopeContainer {
  std::deque<llvm::StringMap<T>> m_scopes{};

public:
  [[nodiscard]] auto all_scopes() const noexcept -> const auto& { return m_scopes; }
  [[nodiscard]] auto last_scope() const noexcept -> const auto& { return m_scopes.back(); }
  [[nodiscard]] auto last_scope() noexcept -> auto& { return m_scopes.back(); }
  [[nodiscard]] auto push_scope_guarded() noexcept -> ScopeContainerGuard<T>;
  void push_scope() noexcept { m_scopes.emplace_back(); }
  void pop_scope() noexcept { m_scopes.pop_back(); }

  auto add(std::string_view key, T object) noexcept { return m_scopes.back().try_emplace(key, move(object)); }
  auto add_to_front(std::string_view key, T object) noexcept { return m_scopes.front().try_emplace(key, move(object)); }
  [[nodiscard]] auto find(std::string_view key) const noexcept -> nullable<const T*> {
    for (const auto& scope : llvm::reverse(m_scopes))
      if (auto lookup = scope.find(key); lookup != scope.end())
        return &lookup->getValue();
    return nullptr;
  }
  [[nodiscard]] auto find(std::string_view key) noexcept -> nullable<T*> {
    for (auto& scope : llvm::reverse(m_scopes))
      if (auto lookup = scope.find(key); lookup != scope.end())
        return &lookup->getValue();
    return nullptr;
  }
  void clear() noexcept { m_scopes.clear(); }
  auto size() noexcept -> size_t { return m_scopes.size(); }
};

template <typename T> class ScopeContainerGuard {
  ScopeContainer<T>& m_container;
  size_t m_prev_size;

public:
  ScopeContainerGuard(ScopeContainer<T>& container, size_t prev_size)
      : m_container(container), m_prev_size(prev_size) {}
  ~ScopeContainerGuard() {
    yume_assert(m_container.size() == m_prev_size, "Cannot pop scope when scope is in invalid state");
    m_container.pop_scope();
  }
  ScopeContainerGuard(const ScopeContainerGuard<T>&) = delete;
  ScopeContainerGuard(ScopeContainerGuard<T>&&) = delete;
  auto operator=(const ScopeContainerGuard<T>&) = delete;
  auto operator=(ScopeContainerGuard<T>&&) = delete;
};

template <typename T> auto ScopeContainer<T>::push_scope_guarded() noexcept -> ScopeContainerGuard<T> {
  push_scope();
  return {*this, size()};
}
} // namespace yume
