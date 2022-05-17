//
// Created by rymiel on 5/12/22.
//
#include "visitor.hpp"
#include "llvm/Support/raw_ostream.h"

namespace yume {
void xml_escape(llvm::raw_ostream& stream, const string& data) {
  for (char i : data) {
    switch (i) {
    case '&': stream << "&amp;"; break;
    case '\'': stream << "&apos;"; break;
    case '\"': stream << "&quot;"; break;
    case '<': stream << "&lt;"; break;
    case '>': stream << "&gt;"; break;
    default: stream << i; break;
    }
  }
}

void DotVisitor::emit_debug_header() {
#ifdef YUME_SPEW_DOT_TOKEN_HEADER
  stream() << m_index << "/" << m_parent;
  if (m_open_parent != -1) {
    stream() << "-" << m_open_parent;
  }
  stream() << "/" << m_children << "<BR/>";
#endif
}

void DotVisitor::header(bool is_inline) {
  if (m_finalized) {
    throw std::runtime_error("Can't visit with DotVisitor when already finalized");
  }

  if (m_write_to_buffer) {
    m_write_to_buffer = false;
    if ((m_children > 0) && m_parent == m_open_parent) {
      m_lines.emplace_back(m_parent, m_index - 1);
      stream() << ">];\n" << AST_KEY << m_index - 1 << " [label=<";
      m_open_parent = -1;
    }
    if (m_open_parent != -1) {
      stream() << "<BR/>";
    }
    stream() << m_buffer;
  }
  if (is_inline) {
    if (m_children == 0) {
      m_buffer = "";
      m_write_to_buffer = true;
    }
    emit_debug_header();
    m_open_parent = m_parent;
  } else {
    if (m_open) {
      stream() << ">];\n";
      m_open = false;
    }
    if (m_index != 0) {
      m_lines.emplace_back(m_parent, m_index);
    }

    stream() << AST_KEY << m_index << " [label=<";
    emit_debug_header();
    m_open = true;
  }
}

void DotVisitor::footer(bool is_inline) {
  if (is_inline) {
    stream() << ">];\n";
    m_open = false;
    m_children++;
  }
  m_index++;
}

void DotVisitor::visit_expr(const ast::Expr& expr, bool is_expr_stat) {
  header(false);

  stream() << "<B>";
  xml_escape(stream(), string(ast::kind_name(expr.kind())));
  if (is_expr_stat) {
    stream() << "<FONT COLOR=\"LIME\">*</FONT>";
  }
  stream() << "</B>";
  auto restore_parent = set_parent(m_index);
  auto restore_children = set_children(0);
  footer(false);
  expr.visit(*this);
  m_parent = restore_parent;
  m_children = ++restore_children;
}

void DotVisitor::visit(const ast::Expr& expr) {
  if (expr.kind() == ast::Kind::ExprStatement) {
    const auto& expr_stat = dynamic_cast<const ast::ExprStatement&>(expr);
    visit_expr(*expr_stat.expr(), true);
  } else {
    visit_expr(expr, false);
  }
}

void DotVisitor::visit(const string& str) {
  header(true);

  stream() << "<I>";
  xml_escape(stream(), str);
  stream() << "</I>";

  footer(true);
}
void DotVisitor::visit(std::nullptr_t) {
  header(true);

  stream() << "<I><FONT COLOR=\"RED\">NULL</FONT></I>";

  footer(true);
}
} // namespace yume
