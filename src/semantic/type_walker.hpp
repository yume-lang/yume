#pragma once

#include "ast/ast.hpp"
#include "ast/crtp_walker.hpp"
#include "util.hpp"
#include <map>
#include <stdexcept>
#include <string>

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
class StructDecl;
class Expr;
class Stmt;
} // namespace ast

namespace semantic {
/// Determine the type information of AST nodes.
/// This makes up most of the "semantic" phase of the compiler.
struct TypeWalker : public CRTPWalker<TypeWalker, false> {
  friend CRTPWalker;

public:
  Compiler& m_compiler;
  ty::Type* m_current_struct{};
  Fn* m_current_fn{};
  std::map<string, ast::AST*> m_scope{};

  /// Whether or not to compile the bodies of methods.  Initially, on the parameter types of methods are traversed and
  /// converted, then everything else in a second pass.
  bool m_in_depth = false;

  explicit TypeWalker(Compiler& compiler) : m_compiler(compiler) {}

  void body_statement(ast::Stmt&);
  void body_expression(ast::Expr&);

private:
  /// Convert an ast type (`ast::Type`) into a type in the type system (`ty::Type`).
  auto convert_type(const ast::Type& ast_type) -> const ty::Type&;

  auto findAllFunctionsByName(ast::CallExpr& call) -> vector<Fn*>;

  template <typename T> void statement([[maybe_unused]] T& stat) {
    throw std::runtime_error("Type walker stubbed on statement "s + stat.kind_name());
  }

  template <typename T> void expression([[maybe_unused]] T& expr) {
    throw std::runtime_error("Type walker stubbed on expression "s + expr.kind_name());
  }
};
} // namespace semantic
} // namespace yume
