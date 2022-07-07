#pragma once

#include "ast/crtp_walker.hpp"
#include "semantic/overload.hpp"
#include "util.hpp"
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
class CallExpr;
class Expr;
class Stmt;
class Type;
} // namespace ast
namespace ty {
class Type;
}
} // namespace yume

namespace yume::semantic {

/// Determine the type information of AST nodes.
/// This makes up most of the "semantic" phase of the compiler.
struct TypeWalker : public CRTPWalker<TypeWalker, false> {
  friend CRTPWalker;

public:
  Compiler& compiler;
  Struct* current_struct{};
  Fn* current_fn{};
  std::map<string, ast::AST*> scope{};

  std::queue<DeclLike> decl_queue{};

  /// Whether or not to compile the bodies of methods.  Initially, on the parameter types of methods are traversed and
  /// converted, then everything else in a second pass.
  bool in_depth = false;

  explicit TypeWalker(Compiler& compiler) : compiler(compiler) {}

  void body_statement(ast::Stmt&);
  void body_expression(ast::Expr&);

  void resolve_queue();

private:
  /// Convert an ast type (`ast::Type`) into a type in the type system (`ty::Type`).
  auto convert_type(const ast::Type& ast_type) -> const ty::Type&;

  auto all_overloads_by_name(ast::CallExpr& call) -> OverloadSet;

  auto with_saved_scope(auto&& callback) {
    // Save everything pertaining to the old context
    auto saved_scope = scope;
    auto* saved_current_fn = current_fn;
    auto* saved_current_struct = current_struct;
    auto saved_depth = in_depth;

    callback();

    // Restore again
    in_depth = saved_depth;
    scope = saved_scope;
    current_fn = saved_current_fn;
    current_struct = saved_current_struct;
  }

  template <typename T> void statement([[maybe_unused]] T& stat) {
    throw std::runtime_error("Type walker stubbed on statement "s + stat.kind_name());
  }

  template <typename T> void expression([[maybe_unused]] T& expr) {
    throw std::runtime_error("Type walker stubbed on expression "s + expr.kind_name());
  }
};
} // namespace yume::semantic
