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
  auto def_clone = def.visit_def([this](auto* ast) -> Def {
    auto* cloned = ast->clone();
    member->body.emplace_back(cloned);
    return cloned;
  });

  auto fn_ptr = std::make_unique<Fn>(def_clone, member, self_ty, subs);
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
  member->body.emplace_back(decl_clone);

  auto st_ptr = std::make_unique<Struct>(*decl_clone, member, self_ty, subs);
  auto new_emplace = instantiations.emplace(subs, move(st_ptr));
  return *new_emplace.first->second;
}

auto Struct::get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Struct&> {
  auto existing_instantiation = instantiations.find(subs);
  if (existing_instantiation == instantiations.end())
    return {false, create_instantiation(subs)};

  return {true, *existing_instantiation->second};
}

auto Fn::name() const noexcept -> string {
  return def.visit_def([](ast::LambdaExpr* /*lambda*/) { return "<lambda>"s; }, // TODO(rymiel): Magic value?
                       [](auto* ast) { return ast->name(); });
}
auto Struct::name() const noexcept -> string { return st_ast.name(); }
auto Const::name() const noexcept -> string { return cn_ast.name; }

auto Fn::ast() -> ast::Stmt& {
  return *def.visit_def([](auto* ast) -> ast::Stmt* { return ast; });
}
auto Fn::ast() const -> const ast::Stmt& {
  return *def.visit_def([](auto* ast) -> const ast::Stmt* { return ast; });
}

auto Fn::ret() const -> optional<ty::Type> {
  return def.visit_def([this](ast::CtorDecl* /*ctor*/) { return self_ty; },
                       [](auto* ast) -> optional<ty::Type> {
                         if (auto& ret = ast->ret(); ret.has_value())
                           return ret->val_ty();
                         return {};
                       });
}

auto Fn::arg_count() const -> size_t {
  return def.visit_def([](auto* decl) { return decl->args().size(); });
}

auto Fn::arg_types() const -> vector<ty::Type> { return visit_map_args(fwd<&ast::TypeName::ensure_ty, ty::Type>); }
auto Fn::arg_names() const -> vector<string> { return visit_map_args(fwd<&ast::TypeName::name, string>); }
auto Fn::arg_nodes() const -> const vector<ast::TypeName>& {
  return def.visit_def([](auto* ast) -> const auto& { return ast->args(); });
}
auto Fn::args() const -> vector<FnArg> {
  return visit_map_args([](auto& arg) { return FnArg(arg.ensure_ty(), arg.name, arg); });
}

auto Fn::varargs() const -> bool {
  return def.visit_def([](ast::FnDecl* fn) { return fn->varargs(); }, always_false);
}
auto Fn::primitive() const -> bool {
  return def.visit_def([](ast::FnDecl* fn) { return fn->primitive(); }, always_false);
}
auto Fn::extern_decl() const -> bool {
  return def.visit_def([](ast::FnDecl* fn) { return fn->extern_decl(); }, always_false);
}
auto Fn::local() const -> bool {
  return def.visit_def([](ast::LambdaExpr* /* lambda */) { return true; }, always_false);
}
auto Fn::extern_linkage() const -> bool {
  return def.visit_def([](ast::FnDecl* fn) { return fn->extern_linkage(); }, always_false);
}
auto Fn::has_annotation(const string& name) const -> bool {
  return def.visit_def([](ast::CtorDecl* /*ctor*/) { return false; },
                       [&name](auto* ast) { return ast->annotations().contains(name); });
}

void Fn::make_extern_linkage(bool value) {
  def.visit_def([value](ast::FnDecl* fn) { return fn->make_extern_linkage(value); },
                always_throw<"Cannot make non-function declaration external">);
}

auto Fn::compound_body() -> ast::Compound& {
  return def.visit_def([](ast::FnDecl* fn_decl) -> auto& { return get<ast::Compound>(fn_decl->body()); },
                       [](ast::CtorDecl* ct_decl) -> auto& { return ct_decl->body(); },
                       [](ast::LambdaExpr* lambda) -> auto& { return lambda->body(); });
}

auto Fn::fn_body() -> ast::FnDecl::body_t& {
  return def.visit_def([](ast::FnDecl* fn_decl) -> auto& { return fn_decl->body(); },
                       always_throw<"Cannot get the function body of non-function declaration", ast::FnDecl::body_t&>);
}
} // namespace yume
