#pragma once

#include "util.hpp"
#include "visitor.hpp"
#include <iosfwd>
#include <string>

namespace yume::ast {
class AST;
}
namespace llvm {
class raw_ostream;
}

namespace yume::diagnostic {
class PrintVisitor : public Visitor {
  llvm::raw_ostream& m_stream;
  bool m_pretty;
  bool m_needs_sep = false;
  int m_indent = 0;

  void header(string_view label);
  void indent(string_view text);
  void dedent(string_view text);

public:
  explicit PrintVisitor(llvm::raw_ostream& stream, bool pretty = false) : m_stream{stream}, m_pretty(pretty) {}
  ~PrintVisitor() override = default;

  PrintVisitor(const PrintVisitor&) = delete;
  PrintVisitor(PrintVisitor&&) = delete;
  auto operator=(const PrintVisitor&) -> PrintVisitor& = delete;
  auto operator=(PrintVisitor&&) -> PrintVisitor& = delete;

  auto visit(const ast::AST& expr, string_view label) -> PrintVisitor& override;
  auto visit(std::nullptr_t null, string_view label) -> PrintVisitor& override;
  auto visit(const string& str, string_view label) -> PrintVisitor& override;
};

inline void PrintVisitor::header(string_view label) {
  if (m_needs_sep)
    m_stream << (m_pretty ? ("\n" + std::string(m_indent * 2UL, ' ')) : " ");
  else
    m_needs_sep = true;

  if (!label.empty())
    m_stream << label << "=";
}

inline void PrintVisitor::indent(string_view text) {
  m_stream << text;
  ++m_indent;
  m_stream << std::string(m_indent * 2UL, ' ');
}

inline void PrintVisitor::dedent(string_view text) {
  m_stream << text;
  --m_indent;
  m_stream << std::string(m_indent * 2UL, ' ');
}

inline auto PrintVisitor::visit(const ast::AST& expr, string_view label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::SAVEDCOLOR, true) << expr.kind_name();
  m_stream.resetColor();
  m_stream << "(";
  if (m_pretty)
    indent("\n");
  m_needs_sep = false;

  expr.visit(*this);
  if (m_pretty)
    dedent("\n");
  m_stream << ")";

  return *this;
}

inline auto PrintVisitor::visit(const string& str, string_view label) -> PrintVisitor& {
  header(label);

  m_stream << '\"';
  m_stream.write_escaped(str);
  m_stream << '\"';

  return *this;
}

inline auto PrintVisitor::visit(std::nullptr_t, string_view label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::RED) << "null";
  m_stream.resetColor();

  return *this;
}
} // namespace yume::diagnostic
