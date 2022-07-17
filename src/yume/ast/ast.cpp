#include "ast.hpp"

#include "token.hpp"
#include "ty/type.hpp"
#include <memory>
#include <stdexcept>

namespace yume::ast {

void AST::unify_val_ty() {
  for (const auto* other : m_attach->depends) {
    if (m_val_ty == other->m_val_ty || other->m_val_ty == nullptr)
      return;

    if (m_val_ty == nullptr) {
      m_val_ty = other->m_val_ty;
    } else {
      const auto* merged = m_val_ty->coalesce(*other->m_val_ty);
      if (merged == nullptr) {
        throw std::logic_error("Conflicting types between AST nodes that are attached: `"s + m_val_ty->name() +
                               "` vs `" + other->m_val_ty->name() + "`!");
      }
      m_val_ty = merged;
    }
  }
}

auto AST::location() const -> Loc {
  if (m_tok.empty())
    return Loc{};
  if (m_tok.size() == 1)
    return m_tok[0].loc;

  return m_tok[0].loc + m_tok[m_tok.size() - 1].loc;
}

void CtorExpr::selected_overload(Ctor* fn) { m_selected_overload = fn; }
auto CtorExpr::selected_overload() const -> Ctor* { return m_selected_overload; }

void CallExpr::selected_overload(Fn* fn) { m_selected_overload = fn; }
auto CallExpr::selected_overload() const -> Fn* { return m_selected_overload; }
} // namespace yume::ast
