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

class DotVisitor : public Visitor {
  static constexpr const char* AST_KEY = "ym_ast_";

  llvm::raw_ostream& m_direct_stream;
  llvm::raw_string_ostream m_buffer_stream;
  bool m_open = false;
  bool m_write_to_buffer = false;
  vector<std::tuple<int, int, string>> m_lines{};
  string m_buffer{};
  string m_prev_label{};
  int m_children{};
  int m_index{};
  int m_parent{};
  int m_open_parent{};

  inline auto set_parent(int new_parent) -> int {
    auto restore_parent = m_parent;
    m_parent = new_parent;
    return restore_parent;
  }
  inline auto set_children(int new_children) -> int {
    auto restore_children = m_children;
    m_children = new_children;
    return restore_children;
  }
  inline auto stream() -> llvm::raw_ostream& {
    if (m_write_to_buffer) {
      return m_buffer_stream;
    }
    return m_direct_stream;
  }
  void header(const char* label, bool is_inline);
  void footer(bool is_inline);
  void emit_debug_header();
  void visit_expr(const ast::AST& expr, const char* label);

public:
  explicit DotVisitor(llvm::raw_ostream& stream_) : m_direct_stream{stream_}, m_buffer_stream{m_buffer} {
    stream() << "digraph \"yume\" {\nnode [shape=box, style=rounded];\n";
  }
  ~DotVisitor() override {
    if (m_write_to_buffer) {
      m_write_to_buffer = false;
      if (m_open_parent != -1) {
        stream() << "<BR/>";
      }
      stream() << m_buffer;
    }
    if (m_open) {
      stream() << ">];\n";
    }
    for (const auto& i : m_lines) {
      stream() << AST_KEY << get<0>(i) << " -> " << AST_KEY << get<1>(i);
      if (auto s = get<2>(i); !s.empty()) {
        stream() << " [label=\"" << s << "\"]";
      }
      stream() << ";\n";
    }
    stream() << "\n}";
    stream().flush();
  }

  auto visit(const ast::AST& expr, const char* label) -> DotVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> DotVisitor& override;

  auto visit(const string& str, const char* label) -> DotVisitor& override;
};
} // namespace yume

#endif // YUME_CPP_VISITOR_HPP
