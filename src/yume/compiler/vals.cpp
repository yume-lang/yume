#include "vals.hpp"
#include "ty/substitution.hpp"
#include <algorithm>
#include <type_traits>

namespace yume {

auto Fn::create_instantiation(Substitution& subs) -> Fn& {
  auto* decl_clone = ast().clone();
  base.member->body().emplace_back(decl_clone);

  auto fn_ptr = std::make_unique<Fn>(*decl_clone, base.self_ty, base.member, subs);
  auto new_emplace = instantiations.emplace(subs, move(fn_ptr));
  return *new_emplace.first->second;
}

auto Fn::get_or_create_instantiation(Substitution& subs) -> std::pair<bool, Fn&> {
  auto existing_instantiation = instantiations.find(subs);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(subs)};

  return {true, *existing_instantiation->second};
}

auto Struct::create_instantiation(Substitution& subs) -> Struct& {
  auto* decl_clone = st_ast.clone();
  member->body().emplace_back(ast::AnyStmt{decl_clone});

  auto st_ptr = std::make_unique<Struct>(*decl_clone, self_ty, member, subs);
  auto new_emplace = instantiations.emplace(subs, move(st_ptr));
  return *new_emplace.first->second;
}

auto Struct::get_or_create_instantiation(Substitution& subs) -> std::pair<bool, Struct&> {
  auto existing_instantiation = instantiations.find(subs);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(subs)};

  return {true, *existing_instantiation->second};
}

auto Fn::name() const -> string { return ast().name(); }
auto Ctor::name() const -> string { return get_self_ty()->name() + ":new"; }
auto Struct::name() const -> string { return st_ast.name(); }

auto Fn::overload_name(const call_t& ast) -> string { return ast.name(); };
auto Ctor::overload_name(const call_t& ast) -> string { return ast.val_ty()->name() + ":new"; };

auto Fn::arg_type(const decl_t::arg_t& ast) -> optional<ty::Type> { return ast.val_ty(); };
auto Ctor::arg_type(const decl_t::arg_t& ast) -> optional<ty::Type> {
  return std::visit([](const auto& t) { return t.val_ty(); }, ast);
};

auto Fn::common_ast(const decl_t::arg_t& ast) -> const ast::AST& { return ast; };
auto Ctor::common_ast(const decl_t::arg_t& ast) -> const ast::AST& {
  return *std::visit([](auto& t) -> const ast::AST* { return &t; }, ast);
};

auto Fn::arg_name(const decl_t::arg_t& ast) -> string { return ast.name; };
auto Ctor::arg_name(const decl_t::arg_t& ast) -> string {
  return std::visit(
      []<typename T>(const T& t) {
        if constexpr (std::is_same_v<T, ast::TypeName>) {
          return t.name;
        } else {
          return t.field();
        }
      },
      ast);
};
} // namespace yume
