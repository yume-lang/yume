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
  bool m_needs_sep = false;

  void header(string_view label);

public:
  explicit PrintVisitor(llvm::raw_ostream& stream) : m_stream{stream} {}
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
    m_stream << " ";
  else
    m_needs_sep = true;

  if (!label.empty())
    m_stream << label << "=";
}

inline auto PrintVisitor::visit(const ast::AST& expr, string_view label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::SAVEDCOLOR, true) << expr.kind_name();
  m_stream.resetColor();
  m_stream << "(";
  m_needs_sep = false;

  expr.visit(*this);
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
