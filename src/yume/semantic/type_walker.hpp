#pragma once

#include "ast/crtp_walker.hpp"
#include "compiler/scope_container.hpp"
#include "semantic/overload.hpp"
#include "ty/type_base.hpp"
#include "util.hpp"
#include <map>
#include <queue>
#include <stdexcept>
#include <string>

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
struct CallExpr;
struct CtorExpr;
class Expr;
class Stmt;
class Type;
} // namespace ast
namespace ty {
class BaseType;
}
} // namespace yume

namespace yume::semantic {

/// Determine the type information of AST nodes.
/// This makes up most of the "semantic" phase of the compiler.
struct TypeWalker : public CRTPWalker<TypeWalker> {
  friend CRTPWalker;

public:
  struct ASTWithName {
    ast::AST* ast;
    string name;
  };

  Compiler& compiler;
  DeclLike current_decl{};
  using scope_t = ScopeContainer<nonnull<ast::AST*>>;
  scope_t scope{};
  vector<scope_t> enclosing_scopes{};
  vector<ASTWithName> closured{};

  std::queue<DeclLike> decl_queue{};

  /// Whether or not to compile the bodies of methods.  Initially, on the parameter types of methods are traversed and
  /// converted, then everything else in a second pass.
  bool in_depth = false;

  explicit TypeWalker(Compiler& compiler) : compiler(compiler) {}

  void body_statement(ast::Stmt&);
  void body_expression(ast::Expr&);

  void resolve_queue();

  auto make_dup(ast::AnyExpr& expr) -> Fn*;

private:
  /// Convert an ast type (`ast::Type`) into a type in the type system (`ty::Type`).
  auto convert_type(ast::Type& ast_type) -> ty::Type;
  auto create_slice_type(const ty::Type& base_type) -> ty::Type;
  void direct_call_operator(ast::CallExpr& expr);

  auto get_or_declare_instantiation(Struct* struct_obj, Substitutions subs) -> ty::Type;

  auto all_fn_overloads_by_name(ast::CallExpr& call) -> OverloadSet;
  auto all_ctor_overloads_by_type(Struct& st, ast::CtorExpr& call) -> OverloadSet;

  auto with_saved_scope(auto&& callback) {
    // Save everything pertaining to the old context
    auto saved_scope = scope;
    auto saved_current_decl = current_decl;
    auto saved_depth = in_depth;
    auto saved_closured = closured;

    callback();

    // Restore again
    closured = saved_closured;
    in_depth = saved_depth;
    current_decl = saved_current_decl;
    scope = saved_scope;
  }

  template <typename T> void statement([[maybe_unused]] T& stat) {
    throw std::runtime_error("Type walker stubbed on statement "s + stat.kind_name());
  }

  template <typename T> void expression([[maybe_unused]] T& expr) {
    throw std::runtime_error("Type walker stubbed on expression "s + expr.kind_name());
  }
};

void make_implicit_conversion(ast::OptionalExpr& expr, optional<ty::Type> target_ty);

} // namespace yume::semantic
