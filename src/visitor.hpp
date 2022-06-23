#pragma once

namespace yume {
class Visitor;
namespace ast {
class AST;
}
} // namespace yume

#include "ast/ast.hpp"
#include "util.hpp"

namespace yume {
class Visitor {
public:
  Visitor() = default;
  virtual ~Visitor() = default;
  Visitor(Visitor&) = delete;
  Visitor(Visitor&&) = default;
  auto operator=(Visitor&) -> Visitor& = delete;
  auto operator=(Visitor&&) -> Visitor& = default;

  virtual auto visit(ast::AST&, const char*) -> Visitor& = 0;

  virtual auto visit(std::nullptr_t, const char*) -> Visitor& = 0;

  virtual auto visit(const string&, const char*) -> Visitor& = 0;

  virtual auto visit(ast::AST& expr) -> Visitor& { return visit(expr, (const char*)nullptr); }

  virtual auto visit(std::nullptr_t) -> Visitor& { return visit(nullptr, (const char*)nullptr); }

  virtual auto visit(const string& str) -> Visitor& { return visit(str, (const char*)nullptr); }

  template <typename T> auto visit(vector<T>& vector, const char* label = nullptr) -> Visitor& {
    Visitor& vis = *this;
    for (auto& i : vector) {
      vis = std::move(vis.visit(i, label));
    }
    return vis;
  }

  template <typename T> auto visit(unique_ptr<T>& ptr, const char* label = nullptr) -> Visitor& {
    if (ptr)
      return visit(*ptr, label);
    return visit(nullptr, label);
  }

  template <typename T> auto visit(optional<T>& opt, const char* label = nullptr) -> Visitor& {
    if (opt.has_value())
      return visit(*opt, label);
    return *this;
  }

  template <typename T> auto visit(std::pair<T, const char*>& pair) -> Visitor& {
    return visit(pair.first, pair.second);
  }

  void visit() {}
};
} // namespace yume
