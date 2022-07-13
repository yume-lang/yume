#include "vals.hpp"
#include "compiler.hpp"
#include <algorithm>
#include <type_traits>

namespace yume {

// XXX: This is worthless. The memoization check is already done within `declare`
auto Fn::declaration(Compiler& compiler, bool mangle) -> llvm::Function* {
  if (base.llvm == nullptr) {
    base.llvm = compiler.declare(*this, mangle);
  }
  return base.llvm;
}

// XXX: This is worthless. The memoization check is already done within `declare`
auto Ctor::declaration(Compiler& compiler) -> llvm::Function* {
  if (base.llvm == nullptr) {
    base.llvm = compiler.declare(*this);
  }
  return base.llvm;
}

auto Fn::create_instantiation(Instantiation& instantiate) -> Fn& {
  auto* decl_clone = ast().clone();
  base.member->direct_body().emplace_back(decl_clone);

  std::map<string, const ty::Type*> subs{};
  for (const auto& [k, v] : instantiate.sub)
    subs.try_emplace(k->name(), v);

  auto fn_ptr = std::make_unique<Fn>(*decl_clone, base.self_ty, base.member, move(subs));
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
  auto* decl_clone = st_ast.clone();
  member->direct_body().emplace_back(decl_clone);

  std::map<string, const ty::Type*> subs{};
  for (const auto& [k, v] : instantiate.sub)
    subs.try_emplace(k->name(), v);

  auto st_ptr = std::make_unique<Struct>(*decl_clone, self_ty, member, move(subs));
  auto new_emplace = instantiations.emplace(instantiate, move(st_ptr));
  return *new_emplace.first->second;
}

auto Struct::get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Struct&> {
  auto existing_instantiation = instantiations.find(instantiate);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(instantiate)};

  return {true, *existing_instantiation->second};
}

auto Fn::name() const -> string { return ast().name(); }
// TODO: Named ctors
auto Ctor::name() const -> string { return base.self_ty->name() + ":new"; }
auto Struct::name() const -> string { return st_ast.name(); }

auto Fn::overload_name(const call_t& ast) -> string { return ast.name(); };
auto Ctor::overload_name(const call_t& ast) -> string { return ast.val_ty()->name() + ":new"; };

auto Fn::arg_type(const decl_t::arg_t& ast) -> const ty::Type* { return ast.val_ty(); };
auto Ctor::arg_type(const decl_t::arg_t& ast) -> const ty::Type* {
  return std::visit([](const auto& t) { return t.val_ty(); }, ast);
};

auto Fn::common_ast(const decl_t::arg_t& ast) -> const ast::AST& { return ast; };
auto Ctor::common_ast(const decl_t::arg_t& ast) -> const ast::AST& {
  return *std::visit([](auto& t) -> const ast::AST* { return &t; }, ast);
};

auto Fn::arg_name(const decl_t::arg_t& ast) -> string { return ast.name(); };
auto Ctor::arg_name(const decl_t::arg_t& ast) -> string {
  return std::visit(
      []<typename T>(const T& t) {
        if constexpr (std::is_same_v<T, ast::TypeName>) {
          return t.name();
        } else {
          return t.field();
        }
      },
      ast);
};
} // namespace yume
