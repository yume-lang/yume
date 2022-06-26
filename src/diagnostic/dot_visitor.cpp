#include "dot_visitor.hpp"
#include "../ast/ast.hpp"
#include "../token.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>

namespace yume::diagnostic {
static void xml_escape(llvm::raw_ostream&& stream, const string& data) {
  for (char i : data) {
    switch (i) {
    case '&': stream << "&amp;"; break;
    case '\'': stream << "&apos;"; break;
    case '\"': stream << "&quot;"; break;
    case '<': stream << "&lt;"; break;
    case '>': stream << "&gt;"; break;
    case '\0': stream << "\\\\0"; break;
    default: stream << i; break;
    }
  }
}

inline static auto opt_str(const char* ptr) -> optional<string> {
  if (ptr == nullptr)
    return {};
  return string(ptr);
}

auto DotVisitor::add_node(yume::Loc location, optional<string>& type, string& kind, const char* label) -> DotNode& {
  return add_node(DotNode(m_index, location, type, kind), label);
}

auto DotVisitor::add_node(const string& content, const char* label) -> DotNode& {
  return add_node(DotNode(m_index, Loc{}, std::nullopt, content), label);
}

auto DotVisitor::add_node(DotNode&& node, const char* label) -> DotNode& {
  m_index++;
  if (m_parent == nullptr) {
    return m_roots.emplace_back(std::move(node));
  }
  return m_parent->children.emplace_back(opt_str(label), std::move(node)).child;
}

void DotVisitor::DotNode::write(llvm::raw_ostream& stream) const {
  stream << AST_KEY << index << " [label=<";
  if (simple()) {
    stream << "<I>" << content << "</I>";
  } else {
    stream << "<FONT POINT-SIZE=\"9\">" << location.to_string() << "</FONT><BR/>";
    if (type.has_value())
      stream << "<U>" << *type << "</U><BR/>";
    stream << "<B>" << content << "</B>";
  }

  bool skip_first_child = false;
  if (!children.empty()) {
    const auto& first = children.at(0);
    if (!first.line_label.has_value() && first.child.simple()) {
      skip_first_child = true;
      stream << "<BR/><I>" << first.child.content << "</I>";
    }
  }

  stream << ">];\n";

  for (const auto& [line_label, child] : children) {
    if (skip_first_child) {
      skip_first_child = false;
      continue;
    }
    child.write(stream);
    stream << AST_KEY << index << " -> " << AST_KEY << child.index;
    if (line_label.has_value())
      stream << " [label=\"" << *line_label << "\"]";
    stream << ";\n";
  }
}

auto DotVisitor::visit(ast::AST& expr, const char* label) -> DotVisitor& {
  Loc location = expr.location();
  optional<string> type = {};
  if (const auto* val_ty = expr.val_ty(); val_ty != nullptr) {
    type = val_ty->name();
  }
  string kind_label;
  xml_escape(llvm::raw_string_ostream(kind_label), expr.kind_name());

  auto& node = add_node(location, type, kind_label, label);

  auto* restore_parent = std::exchange(m_parent, &node);
  expr.visit(*this);
  m_parent = restore_parent;
  // TODO: The visitor could write the node out to the stream if restore_parent was nullptr (i.e. there was no parent),
  // as it means this object was a root. This could get rid of the m_roots member variable and also remove the stream
  // code from the destructor.

  return *this;
}

auto DotVisitor::visit(const string& str, const char* label) -> DotVisitor& {
  string content;
  xml_escape(llvm::raw_string_ostream(content), str);

  add_node(content, label);

  return *this;
}

auto DotVisitor::visit(std::nullptr_t, const char* label) -> DotVisitor& {
  add_node("<FONT COLOR=\"RED\">NULL</FONT>", label);

  return *this;
}
} // namespace yume::diagnostic
