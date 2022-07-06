#include "hash_visitor.hpp"
#include "ast/ast.hpp"
#include <cstddef>

namespace yume::diagnostic {
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
