#pragma once

#include "util.hpp"
#include "visitor.hpp"
#include <iosfwd>
#include <limits>
#include <numbers>
#include <string>

namespace yume::ast {
class AST;
}
namespace llvm {
class raw_ostream;
}

namespace yume::diagnostic {
class HashVisitor : public Visitor {
  uint64_t& m_seed;

  template <typename T> static void hash_combine(uint64_t& seed, const T& v) {
    static constexpr auto PHI_FRAC = std::numbers::phi_v<long double> - 1;
    static constexpr auto ALL_BITS = std::numeric_limits<std::size_t>::max();
    static constexpr auto FLOATING_HASH_CONST = PHI_FRAC * ALL_BITS;
    static constexpr auto HASH_CONST = static_cast<std::size_t>(FLOATING_HASH_CONST);
    static constexpr auto TWIST_LEFT = 6;
    static constexpr auto TWIST_RIGHT = 2;

    std::hash<T> hasher;
    seed ^= hasher(v) + HASH_CONST + (seed << TWIST_LEFT) + (seed >> TWIST_RIGHT);
  }

public:
  explicit HashVisitor(uint64_t& seed) : m_seed(seed){};
  ~HashVisitor() override = default;

  HashVisitor(const HashVisitor&) = delete;
  HashVisitor(HashVisitor&&) = delete;
  auto operator=(const HashVisitor&) -> HashVisitor& = delete;
  auto operator=(HashVisitor&&) -> HashVisitor& = delete;

  auto visit(const ast::AST& expr, const char* label) -> HashVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> HashVisitor& override;

  auto visit(const string& str, const char* label) -> HashVisitor& override;
};

inline auto HashVisitor::visit(const ast::AST& expr, const char* label) -> HashVisitor& {
  hash_combine(m_seed, label);
  hash_combine(m_seed, expr.kind_name());
  hash_combine(m_seed, expr.describe());
  expr.visit(*this);

  return *this;
}

inline auto HashVisitor::visit(const string& str, const char* label) -> HashVisitor& {
  hash_combine(m_seed, label);
  hash_combine(m_seed, str);

  return *this;
}

inline auto HashVisitor::visit(std::nullptr_t, const char* label) -> HashVisitor& {
  hash_combine(m_seed, label);
  hash_combine(m_seed, nullptr);

  return *this;
}
} // namespace yume::diagnostic
