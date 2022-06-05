#pragma once

#include "../util.hpp"
#include "../visitor.hpp"
#include <iosfwd>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <tuple>
#include <vector>

namespace yume::ast {
class AST;
}

namespace yume::diagnostic {
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
  DotVisitor(const DotVisitor&) = delete;
  DotVisitor(DotVisitor&&) = delete;
  auto operator=(const DotVisitor&) -> DotVisitor& = delete;
  auto operator=(DotVisitor&&) -> DotVisitor& = delete;

  auto visit(const ast::AST& expr, const char* label) -> DotVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> DotVisitor& override;

  auto visit(const string& str, const char* label) -> DotVisitor& override;
};
} // namespace yume::diagnostic
