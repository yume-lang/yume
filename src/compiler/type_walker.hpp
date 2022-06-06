#pragma once

#include "../visitor.hpp"
#include "crtp_walker.hpp"
#include <string>

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
}

struct TypeWalker : public CRTPWalker<TypeWalker, false>, public Visitor {
  friend CRTPWalker;

public:
  Compiler& m_compiler;
  Fn* m_current_fn{};
  std::map<string, ast::AST*> m_scope{};

  // Whether or not to compile the bodies of methods.  Initially, on the parameter types of methods are traversed and
  // converted, then everything else in a second pass.
  bool m_in_depth = false;

  explicit TypeWalker(Compiler& compiler) : m_compiler(compiler) {}

  auto visit(ast::AST& expr, const char* label) -> TypeWalker& override;

  inline auto visit([[maybe_unused]] std::nullptr_t null, [[maybe_unused]] const char* label) -> TypeWalker& override {
    return *this;
  }

  inline auto visit([[maybe_unused]] const string& str, [[maybe_unused]] const char* label) -> TypeWalker& override {
    return *this;
  }

  void body_statement(ast::Stmt&);
  void body_expression(ast::Expr&);
private:

  template <typename T> inline void statement([[maybe_unused]] T& stat) {
#ifdef YUME_SPEW_TYPE_WALKER_STUB
    std::cerr << "Type walked stubbed on statement " << ast::kind_name(stat.kind()) << "\n";
#endif
    stat.visit(*this);
  }

  template <typename T> inline void expression([[maybe_unused]] T& expr) {
#ifdef YUME_SPEW_TYPE_WALKER_STUB
    std::cerr << "Type walked stubbed on expression " << ast::kind_name(expr.kind()) << "\n";
#endif
    expr.visit(*this);
  }
};
} // namespace yume
