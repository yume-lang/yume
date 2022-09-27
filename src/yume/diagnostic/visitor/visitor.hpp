#pragma once

#include "ast/ast.hpp"
#include "util.hpp"

namespace yume {
class Visitor;
namespace ast {
class AST;
}
} // namespace yume

namespace yume {
class Visitor {
public:
  Visitor() = default;
  virtual ~Visitor() = default;
  Visitor(Visitor&) = delete;
  Visitor(Visitor&&) = default;
  auto operator=(Visitor&) -> Visitor& = delete;
  auto operator=(Visitor&&) -> Visitor& = default;

  virtual auto visit(const ast::AST&, string_view) -> Visitor& = 0;
  virtual auto visit(std::nullptr_t, string_view) -> Visitor& = 0;
  virtual auto visit(const string&, string_view) -> Visitor& = 0;
};
} // namespace yume
