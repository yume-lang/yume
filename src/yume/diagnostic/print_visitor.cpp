#include "print_visitor.hpp"
#include "ast/ast.hpp"
#include "util.hpp"
#include "llvm/Support/raw_ostream.h"

namespace yume::diagnostic {
void PrintVisitor::header(const char* label) {
  if (m_needs_sep)
    m_stream << " ";
  else
    m_needs_sep = true;

  if (label != nullptr)
    m_stream << label << "=";
}

auto PrintVisitor::visit(const ast::AST& expr, const char* label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::SAVEDCOLOR, true) << expr.kind_name();
  m_stream.resetColor();
  m_stream << "(";
  m_needs_sep = false;

  expr.visit(*this);
  m_stream << ")";

  return *this;
}

auto PrintVisitor::visit(const string& str, const char* label) -> PrintVisitor& {
  header(label);

  m_stream << '\"';
  m_stream.write_escaped(str);
  m_stream << '\"';

  return *this;
}

auto PrintVisitor::visit(std::nullptr_t, const char* label) -> PrintVisitor& {
  header(label);

  m_stream.changeColor(llvm::raw_ostream::RED) << "null";
  m_stream.resetColor();

  return *this;
}
} // namespace yume::diagnostic
