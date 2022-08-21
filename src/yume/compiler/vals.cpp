#include "vals.hpp"
#include "ast/ast.hpp"
#include "ty/substitution.hpp"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace yume {
static constexpr const auto always_false = [](auto&&... /* ignored */) { return false; };

template <StringLiteral msg, typename RetT = void>
static constexpr const auto always_throw = [](auto&&... /* ignored */) -> RetT { throw std::runtime_error(msg.value); };

template <auto pm, typename R = void>
static constexpr const auto fwd = [](auto&&... args) -> R { return std::invoke(pm, args...); };
template <auto pm>
static constexpr const auto fwd<pm, void> = [](auto&&... args) -> decltype(auto) { return std::invoke(pm, args...); };

auto Fn::create_instantiation(Substitution& subs) noexcept -> Fn& {
  auto* decl_clone = ast().clone();
  member->body().emplace_back(decl_clone);

  auto fn_ptr = std::make_unique<Fn>(*decl_clone, self_ty, member, subs);
  auto new_emplace = instantiations.emplace(subs, move(fn_ptr));
  return *new_emplace.first->second;
}

auto Fn::get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Fn&> {
  auto existing_instantiation = instantiations.find(subs);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(subs)};

  return {true, *existing_instantiation->second};
}

auto Struct::create_instantiation(Substitution& subs) noexcept -> Struct& {
  auto* decl_clone = st_ast.clone();
  member->body().emplace_back(decl_clone);

  auto st_ptr = std::make_unique<Struct>(*decl_clone, self_ty, member, subs);
  auto new_emplace = instantiations.emplace(subs, move(st_ptr));
  return *new_emplace.first->second;
}

auto Struct::get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Struct&> {
  auto existing_instantiation = instantiations.find(subs);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(subs)};

  return {true, *existing_instantiation->second};
}

auto Fn::name() const noexcept -> string { return ast().name(); }
auto Struct::name() const noexcept -> string { return st_ast.name(); }

auto Fn::ret() const -> optional<ty::Type> {
  if (auto* fn_decl = dyn_cast<ast::FnDecl>(&ast_decl)) {
    if (auto& ret = fn_decl->ret(); ret.has_value())
      return ret->val_ty();
    return {};
  }
  return self_ty;
}

auto Fn::arg_count() const -> size_t {
  return visit_decl([](auto& decl) { return decl.args().size(); });
}

auto Fn::arg_types() const -> vector<ty::Type> {
  return visit_map_args([](auto& arg) { return arg.ensure_ty(); });
}
auto Fn::arg_names() const -> vector<string> { return visit_map_args(fwd<&ast::TypeName::name, string>); }
auto Fn::arg_nodes() const -> const vector<ast::TypeName>& {
  return visit_decl([](auto& fn_decl) -> const auto& { return fn_decl.args(); });
}
auto Fn::args() const -> vector<FnArg> {
  return visit_map_args([](auto& arg) { return FnArg(arg.ensure_ty(), arg.name, arg); });
}

auto Fn::varargs() const -> bool { return visit_decl(fwd<&ast::FnDecl::varargs>, always_false); }
auto Fn::primitive() const -> bool { return visit_decl(fwd<&ast::FnDecl::primitive>, always_false); }
auto Fn::extern_decl() const -> bool { return visit_decl(fwd<&ast::FnDecl::extern_decl>, always_false); }
auto Fn::extern_linkage() const -> bool { return visit_decl(fwd<&ast::FnDecl::extern_linkage>, always_false); }

void Fn::make_extern_linkage(bool value) {
  visit_decl(fwd<&ast::FnDecl::make_extern_linkage>, always_throw<"Cannot make non-function declaration external">,
             value);
}

auto Fn::compound_body() const -> const ast::Compound& {
  return visit_decl([](ast::FnDecl& fn_decl) -> auto& { return get<ast::Compound>(fn_decl.body()); },
                    [](ast::CtorDecl& ct_decl) -> auto& { return ct_decl.body(); });
}

auto Fn::fn_body() const -> const ast::FnDecl::body_t& {
  return visit_decl([](ast::FnDecl& fn_decl) -> auto& { return fn_decl.body(); },
                    always_throw<"Cannot get the function body of non-function declaration", ast::FnDecl::body_t&>);
}
} // namespace yume
