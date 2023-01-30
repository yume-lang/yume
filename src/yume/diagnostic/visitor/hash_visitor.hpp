#pragma once

#include "util.hpp"
#include "visitor.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <string>

namespace yume::ast {
class AST;
}

namespace yume::diagnostic {
class HashVisitor : public Visitor {
  uint64_t& m_seed;

public:
  explicit HashVisitor(uint64_t& seed) : m_seed(seed) {}
  ~HashVisitor() override = default;

  HashVisitor(const HashVisitor&) = delete;
  HashVisitor(HashVisitor&&) = delete;
  auto operator=(const HashVisitor&) -> HashVisitor& = delete;
  auto operator=(HashVisitor&&) -> HashVisitor& = delete;

  auto visit(const ast::AST& expr, string_view label) -> HashVisitor& override;
  auto visit(std::nullptr_t null, string_view label) -> HashVisitor& override;
  auto visit(const string& str, string_view label) -> HashVisitor& override;
};
} // namespace yume::diagnostic
