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

  void header(const char* label);

public:
  explicit PrintVisitor(llvm::raw_ostream& stream_) : m_stream{stream_} {}
  ~PrintVisitor() override = default;

  PrintVisitor(const PrintVisitor&) = delete;
  PrintVisitor(PrintVisitor&&) = delete;
  auto operator=(const PrintVisitor&) -> PrintVisitor& = delete;
  auto operator=(PrintVisitor&&) -> PrintVisitor& = delete;

  auto visit(const ast::AST& expr, const char* label) -> PrintVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> PrintVisitor& override;

  auto visit(const string& str, const char* label) -> PrintVisitor& override;
};

inline void PrintVisitor::header(const char* label) {
  if (m_needs_sep)
    m_stream << " ";
  else
    m_needs_sep = true;

  if (label != nullptr)
    m_stream << label << "=";
}

inline auto PrintVisitor::visit(const ast::AST& expr, const char* label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::SAVEDCOLOR, true) << expr.kind_name();
  m_stream.resetColor();
  m_stream << "(";
  m_needs_sep = false;

  expr.visit(*this);
  m_stream << ")";

  return *this;
}

inline auto PrintVisitor::visit(const string& str, const char* label) -> PrintVisitor& {
  header(label);

  m_stream << '\"';
  m_stream.write_escaped(str);
  m_stream << '\"';

  return *this;
}

inline auto PrintVisitor::visit(std::nullptr_t, const char* label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::RED) << "null";
  m_stream.resetColor();

  return *this;
}
} // namespace yume::diagnostic
