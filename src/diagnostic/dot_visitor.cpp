#include "dot_visitor.hpp"
#include "../ast.hpp"
#include "../token.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

namespace yume::diagnostic {
static void xml_escape(llvm::raw_ostream& stream, const string& data) {
  for (char i : data) {
    switch (i) {
    case '&': stream << "&amp;"; break;
    case '\'': stream << "&apos;"; break;
    case '\"': stream << "&quot;"; break;
    case '<': stream << "&lt;"; break;
    case '>': stream << "&gt;"; break;
    case '\x00': stream << "\\\\x00"; break;
    default: stream << i; break;
    }
  }
}

void DotVisitor::emit_debug_header() {
#ifdef YUME_SPEW_DOT_TOKEN_HEADER
  stream() << "<FONT POINT-SIZE=\"6\">" << m_index << "/" << m_parent;
  if (m_open_parent != -1) {
    stream() << "-" << m_open_parent;
  }
  stream() << "/" << m_children << "</FONT><BR/>";
#endif
}

void DotVisitor::header(const char* label, bool is_inline) {
  if (m_write_to_buffer) {
    m_write_to_buffer = false;
    if ((m_children > 0) && m_parent == m_open_parent) {
      m_lines.emplace_back(m_parent, m_index - 1, m_prev_label);
      stream() << ">];\n" << AST_KEY << m_index - 1 << " [label=<";
      m_open_parent = -1;
    }
    if (m_open_parent != -1) {
      stream() << "<BR/>";
    }
    stream() << m_buffer;
  }
  if (is_inline && m_children == 0) {
    m_buffer = "";
    m_write_to_buffer = true;
    emit_debug_header();
    m_open_parent = m_parent;
  } else {
    if (m_open) {
      stream() << ">];\n";
      m_open = false;
    }
    if (m_index != 0) {
      m_lines.emplace_back(m_parent, m_index, label == nullptr ? "" : label);
    }

    stream() << AST_KEY << m_index << " [label=<";
    emit_debug_header();
    m_open = true;
  }

  m_prev_label = label == nullptr ? "" : label;
}

void DotVisitor::footer(bool is_inline) {
  if (is_inline) {
    stream() << ">];\n";
    m_open = false;
    m_children++;
  }
  m_index++;
}

void DotVisitor::visit_expr(ast::AST& expr, const char* label) {
  header(label, false);

  stream() << "<FONT POINT-SIZE=\"9\">" << expr.location().to_string() << "</FONT><BR/>";
  if (auto* val_ty = expr.val_ty(); val_ty != nullptr) {
    stream() << "<U>" << val_ty->name() << "</U><BR/>";
  }
  stream() << "<B>";
  xml_escape(stream(), expr.kind_name());
  stream() << "</B>";
  auto restore_parent = set_parent(m_index);
  auto restore_children = set_children(0);
  footer(false);
  expr.visit(*this);
  m_parent = restore_parent;
  m_children = ++restore_children;
}

auto DotVisitor::visit(ast::AST& expr, const char* label) -> DotVisitor& {
  visit_expr(expr, label);
  return *this;
}

auto DotVisitor::visit(const string& str, const char* label) -> DotVisitor& {
  header(label, label == nullptr);

  stream() << "<I>";
  xml_escape(stream(), str);
  stream() << "</I>";

  footer(label == nullptr);
  return *this;
}
auto DotVisitor::visit(std::nullptr_t, const char* label) -> DotVisitor& {
  header(label, label == nullptr);

  stream() << "<I><FONT COLOR=\"RED\">NULL</FONT></I>";

  footer(label == nullptr);
  return *this;
}
} // namespace yume::diagnostic
