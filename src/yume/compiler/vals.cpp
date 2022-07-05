#include "vals.hpp"
#include "compiler.hpp"
#include <algorithm>
#include <type_traits>

namespace yume {

auto Fn::declaration(Compiler& compiler, bool mangle) -> llvm::Function* {
  if (m_llvm_fn == nullptr) {
    m_llvm_fn = compiler.declare(*this, mangle);
  }
  return m_llvm_fn;
}

auto Fn::create_instantiation(Instantiation& instantiate) -> Fn& {
  auto* decl_clone = m_ast_decl.clone();
  m_member->direct_body().emplace_back(decl_clone);

  std::map<string, const ty::Type*> subs{};
  for (const auto& [k, v] : instantiate.sub)
    subs.try_emplace(k->name(), v);

  auto fn_ptr = std::make_unique<Fn>(*decl_clone, m_parent, m_member, move(subs));
  auto new_emplace = m_instantiations.emplace(instantiate, move(fn_ptr));
  return *new_emplace.first->second;
}

auto Fn::get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Fn&> {
  auto existing_instantiation = m_instantiations.find(instantiate);
  if (existing_instantiation == m_instantiations.end())
    return {false, create_instantiation(instantiate)};

  return {true, *existing_instantiation->second};
};
} // namespace yume
