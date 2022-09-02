#pragma once

#include "token.hpp"
#include "util.hpp"
#include "visitor.hpp"
#include <iosfwd>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yume::ast {
class AST;
}

namespace yume::diagnostic {
class DotVisitor final : public Visitor {
  static constexpr const char* AST_KEY = "ym_ast_";

  struct DotConnection;

  struct DotNode {
    int index;
    optional<Loc> location;
    optional<string> type;
    string content;
    vector<DotConnection> children{};

    DotNode(int index, optional<Loc> location, optional<string> type, string kind)
        : index(index), location(move(location)), type(move(type)), content(move(kind)) {}

    [[nodiscard]] auto simple() const -> bool { return !location.has_value(); }

    void write(llvm::raw_ostream& stream) const;
  };

  struct DotConnection {
    optional<string> line_label;
    DotNode child;

    DotConnection(optional<string> line_label, DotNode child) noexcept
        : line_label(move(line_label)), child(move(child)) {}
  };

  llvm::raw_ostream& m_stream;
  int m_index{};
  DotNode* m_parent{};
  unique_ptr<DotNode> m_root{};

  auto add_node(const string& content, const char* label) -> DotNode&;
  auto add_node(DotNode&& node, const char* label) -> DotNode&;

public:
  explicit DotVisitor(llvm::raw_ostream& stream) : m_stream{stream} {
    m_stream << "digraph \"yume\" {\nnode [shape=box, style=rounded];\n";
  }
  ~DotVisitor() override {
    m_stream << "\n}";
    m_stream.flush();
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
