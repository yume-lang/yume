//
// Created by rymiel on 5/12/22.
//

#ifndef YUME_CPP_VISITOR_HPP
#define YUME_CPP_VISITOR_HPP

namespace yume {
class Visitor;
namespace ast {
class AST;
} // namespace ast
} // namespace yume

#include "ast.hpp"
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

  virtual auto visit(const ast::AST&, const char*) -> Visitor& = 0;

  virtual auto visit(std::nullptr_t, const char*) -> Visitor& = 0;

  virtual auto visit(const string&, const char*) -> Visitor& = 0;

  inline virtual auto visit(const ast::AST& expr) -> Visitor& { return visit(expr, (const char*)nullptr); }

  inline virtual auto visit(std::nullptr_t) -> Visitor& { return visit(nullptr, (const char*)nullptr); }

  inline virtual auto visit(const string& str) -> Visitor& { return visit(str, (const char*)nullptr); }

  template <typename T> inline auto visit(const vector<T>& vector, const char* label = nullptr) -> Visitor& {
    Visitor& vis = *this;
    for (const auto& i : vector) {
      vis = std::move(vis.visit(i, label));
    }
    return vis;
  }

  template <typename T> inline auto visit(const unique_ptr<T>& ptr, const char* label = nullptr) -> Visitor& {
    if (ptr) {
      return visit(*ptr, label);
    }
    return visit(nullptr, label);
  }

  template <typename T> inline auto visit(const optional<T>& opt, const char* label = nullptr) -> Visitor& {
    if (opt.has_value()) {
      return visit(*opt, label);
    }
    return *this;
  }

  template <typename T> inline auto visit(const std::pair<T, const char*>& pair) -> Visitor& {
    return visit(pair.first, pair.second);
  }

  inline void visit() {}
};
} // namespace yume

#endif // YUME_CPP_VISITOR_HPP
