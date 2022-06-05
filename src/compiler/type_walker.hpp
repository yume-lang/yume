#pragma once

#include "../visitor.hpp"
#include <string>

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
}

struct TypeWalkVisitor : public Visitor {
  Compiler& m_compiler;
  Fn* m_current_fn{};
  // Whether or not to compile the bodies of methods.  Initially, on the parameter types of methods are traversed and
  // converted, then everything else in a second pass.
  bool m_in_depth = false;

public:
  explicit TypeWalkVisitor(Compiler& compiler) : m_compiler(compiler) {}

  auto visit(ast::AST& expr, const char* label) -> TypeWalkVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> TypeWalkVisitor& override;

  auto visit(const string& str, const char* label) -> TypeWalkVisitor& override;
};
} // namespace yume
