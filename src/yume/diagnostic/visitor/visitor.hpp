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

  virtual auto visit(const ast::AST&, const char*) -> Visitor& = 0;

  virtual auto visit(std::nullptr_t, const char*) -> Visitor& = 0;

  virtual auto visit(const string&, const char*) -> Visitor& = 0;

  virtual auto visit(const ast::AST& expr) -> Visitor& { return visit(expr, static_cast<const char*>(nullptr)); }

  virtual auto visit(std::nullptr_t) -> Visitor& { return visit(nullptr, static_cast<const char*>(nullptr)); }

  virtual auto visit(const string& str) -> Visitor& { return visit(str, static_cast<const char*>(nullptr)); }

  template <typename T> auto visit(const ast::OptionalAnyBase<T>& any_base, const char* label = nullptr) -> Visitor& {
    if (any_base.raw_ptr() != nullptr)
      return visit(*any_base, label);
    return visit(nullptr, label);
  }

  template <typename T> auto visit(const ast::AnyBase<T>& any_base, const char* label = nullptr) -> Visitor& {
    yume_assert(any_base.raw_ptr() != nullptr, "AnyBase should never be null");
    return visit(*any_base, label);
  }

  template <std::ranges::range Range>
  requires (!std::convertible_to<Range, const char*>)
  auto visit(const Range& iter, const char* label = nullptr) -> Visitor& {
    Visitor& vis = *this;
    for (auto& i : iter) {
      vis = move(vis.visit(i, label));
    }
    return vis;
  }

  template <typename T> auto visit(const optional<T>& opt, const char* label = nullptr) -> Visitor& {
    if (opt.has_value())
      return visit(*opt, label);
    return *this;
  }

  template <typename T, typename U> auto visit(const std::variant<T, U>& var, const char* label = nullptr) -> Visitor& {
    if (std::holds_alternative<T>(var))
      return visit(std::get<T>(var), label);
    return visit(std::get<U>(var), label);
  }

  template <typename T> auto visit(std::pair<T, const char*>& pair) -> Visitor& {
    return visit(pair.first, pair.second);
  }

  void visit() {}
};
} // namespace yume
