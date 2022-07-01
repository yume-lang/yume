#pragma once

#include "util.hpp"
#include "visitor.hpp"
#include <iosfwd>
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
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b97f4a7c15 + (seed << 6) + (seed >> 2); // NOLINT
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
