#include "vals.hpp"
#include "ast/ast.hpp"
#include "ty/substitution.hpp"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace yume {

[[nodiscard]] static auto arg_type(const ast::CtorDecl::arg_t& ast) -> optional<ty::Type> {
  return std::visit([](const auto& t) { return t.val_ty(); }, ast);
}

[[nodiscard]] static auto arg_name(const ast::CtorDecl::arg_t& ast) -> string {
  return std::visit(
      []<typename T>(const T& t) {
        if constexpr (std::is_same_v<T, ast::TypeName>) {
          return t.name;
        } else {
          return t.field();
        }
      },
      ast);
}

[[nodiscard]] static auto arg_ast(const ast::CtorDecl::arg_t& ast) -> const ast::AST& {
  return *std::visit([](auto& t) -> const ast::AST* { return &t; }, ast);
}

static constexpr const auto always_false = [](auto&&... /* ignored */) { return false; };

template <StringLiteral msg, typename RetT = void>
static constexpr const auto always_throw = [](auto&&... /* ignored */) -> RetT { throw std::runtime_error(msg.value); };

template <auto pm>
requires std::is_member_function_pointer_v<decltype(pm)>
static constexpr const auto fwd = [](auto&&... args) -> decltype(auto) { return std::invoke(pm, args...); };

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
  return visit_decl([](ast::FnDecl& fn_decl) { return fn_decl.args().size(); },
                    [](ast::CtorDecl& ct_decl) { return ct_decl.args().size(); });
}

auto Fn::arg_types() const -> vector<ty::Type> {
  return visit_map_args<ty::Type>([](auto& fn_arg) { return fn_arg.ensure_ty(); },
                                  [](auto& ct_arg) { return *arg_type(ct_arg); });
}
auto Fn::arg_names() const -> vector<string> {
  return visit_map_args<string>([](auto& fn_arg) { return fn_arg.name; },
                                [](auto& ct_arg) { return arg_name(ct_arg); });
}
auto Fn::args() const -> vector<FnArg> {
  return visit_map_args<FnArg>(
      [](auto& fn_arg) { return FnArg(fn_arg.ensure_ty(), fn_arg.name, fn_arg); },
      [](auto& ct_arg) { return FnArg(*arg_type(ct_arg), arg_name(ct_arg), arg_ast(ct_arg)); });
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
