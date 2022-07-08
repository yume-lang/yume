#include "ast.hpp"

#include "atom.hpp"
#include "diagnostic/errors.hpp"
#include "diagnostic/source_location.hpp"
#include "qualifier.hpp"
#include "token.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <llvm/Support/raw_ostream.h>
#include <memory>

namespace yume::ast {

void AST::unify_val_ty() const {
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

} // namespace yume::ast
