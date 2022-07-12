#include "vals.hpp"
#include "compiler.hpp"
#include <algorithm>
#include <type_traits>

namespace yume {

auto Fn::declaration(Compiler& compiler, bool mangle) -> llvm::Function* {
  if (llvm == nullptr) {
    llvm = compiler.declare(*this, mangle);
  }
  return llvm;
}

auto Fn::create_instantiation(Instantiation& instantiate) -> Fn& {
  auto* decl_clone = ast.clone();
  member->direct_body().emplace_back(decl_clone);

  std::map<string, const ty::Type*> subs{};
  for (const auto& [k, v] : instantiate.sub)
    subs.try_emplace(k->name(), v);

  auto fn_ptr = std::make_unique<Fn>(*decl_clone, self_t, member, move(subs));
  auto new_emplace = instantiations.emplace(instantiate, move(fn_ptr));
  return *new_emplace.first->second;
}

auto Fn::get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Fn&> {
  auto existing_instantiation = instantiations.find(instantiate);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(instantiate)};

  return {true, *existing_instantiation->second};
}

auto Struct::create_instantiation(Instantiation& instantiate) -> Struct& {
  auto* decl_clone = ast.clone();
  member->direct_body().emplace_back(decl_clone);

  std::map<string, const ty::Type*> subs{};
  for (const auto& [k, v] : instantiate.sub)
    subs.try_emplace(k->name(), v);

  auto st_ptr = std::make_unique<Struct>(*decl_clone, self_t, member, move(subs));
  auto new_emplace = instantiations.emplace(instantiate, move(st_ptr));
  return *new_emplace.first->second;
}

auto Struct::get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Struct&> {
  auto existing_instantiation = instantiations.find(instantiate);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(instantiate)};

  return {true, *existing_instantiation->second};
}
} // namespace yume
