#pragma once

#include <string>
#include "../visitor.hpp"

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
}

struct TypeWalkVisitor : public Visitor {
  Compiler& m_compiler;
  Fn* m_current_fn{};

public:
  explicit TypeWalkVisitor(Compiler& compiler) : m_compiler(compiler) {}

  auto visit(ast::AST& expr, const char* label) -> TypeWalkVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> TypeWalkVisitor& override;

  auto visit(const string& str, const char* label) -> TypeWalkVisitor& override;
};
} // namespace yume
