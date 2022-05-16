//
// Created by rymiel on 5/12/22.
//

#ifndef YUME_CPP_VISITOR_HPP
#define YUME_CPP_VISITOR_HPP

namespace yume {
class Visitor;
namespace ast {
struct Expr;
struct ExprStatement;
}
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

  virtual void visit(const ast::Expr&) = 0;

  virtual void visit(std::nullptr_t) = 0;

  virtual void visit(const string&) = 0;

  template <typename T> inline void visit(const vector<T>& vector) {
    for (const auto& i : vector) {
      visit(i);
    }
  }

  template <typename T> inline void visit(const unique_ptr<T>& ptr) {
    if (ptr) {
      visit(*ptr);
    } else {
      visit(nullptr);
    }
  }

  template <typename T> inline void visit(const optional<T>& opt) {
    if (opt.has_value()) {
      visit(*opt);
    } else {
      visit(nullptr);
    }
  }

  template <typename T, typename U, typename... Ts> inline void visit(T&& t, U&& u, Ts&&... ts) {
    visit(t);
    visit(u);
    visit(ts...);
  }

  inline void visit() {}
};

class DotVisitor : public Visitor {
  static constexpr const char* AST_KEY = "ym_ast_";

  llvm::raw_ostream& m_stream;
  bool m_finalized = false;
  int m_index{};
  int m_parent{};

  inline auto set_parent(int new_parent) -> int {
    auto restore_parent = m_parent;
    m_parent = new_parent;
    return restore_parent;
  }
  void visit_expr(const ast::Expr& expr, bool is_expr_stat);

public:
  explicit DotVisitor(llvm::raw_ostream& stream) : m_stream{stream} {
    m_stream << "digraph \"yume\" {\nnode [shape=box, style=rounded];\n";
  };
  ~DotVisitor() override = default;

  void visit(const ast::Expr& expr) override;

  void visit(std::nullptr_t null) override;

  void visit(const string& str) override;

  inline void finalize() {
    m_finalized = true;
    m_stream << "}";
    m_stream.flush();
  }
};
} // namespace yume

#endif // YUME_CPP_VISITOR_HPP
