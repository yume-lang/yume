//
// Created by rymiel on 5/12/22.
//
#include "visitor.hpp"

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

void DotVisitor::visit_expr(const ast::Expr& expr, bool is_expr_stat) {
  if (m_finalized) {
    throw std::runtime_error("Can't visit with DotVisitor when already finalized");
  }

  if (m_index != 0) {
    m_stream << AST_KEY << m_parent << " -> " << AST_KEY << m_index << ";\n";
  }
  m_stream << AST_KEY << m_index << " [label=<<B>";
  xml_escape(m_stream, string(ast::kind_name(expr.kind())));
  if (is_expr_stat) {
    m_stream << "<FONT COLOR=\"LIME\">*</FONT>";
  }
  m_stream << "</B>>];\n";
  auto restore_parent = set_parent(m_index);
  m_index++;
  expr.visit(*this);
  m_parent = restore_parent;
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
  if (m_finalized) {
    throw std::runtime_error("Can't visit with DotVisitor when already finalized");
  }

  if (m_index != 0) {
    m_stream << AST_KEY << m_parent << " -> " << AST_KEY << m_index << ";\n";
  }
  m_stream << AST_KEY << m_index << " [label=<<I>";
  xml_escape(m_stream, str);
  m_stream << "</I>>];\n";
  m_index++;
}
void DotVisitor::visit(std::nullptr_t) {
  if (m_finalized) {
    throw std::runtime_error("Can't visit with DotVisitor when already finalized");
  }

  if (m_index != 0) {
    m_stream << AST_KEY << m_parent << " -> " << AST_KEY << m_index << ";\n";
  }
  m_stream << AST_KEY << m_index << " [label=<<I><FONT COLOR=\"RED\">NULL</FONT></I>>];\n";
  m_index++;
}
} // namespace yume
