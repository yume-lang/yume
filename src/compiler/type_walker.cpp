#include "type_walker.hpp"
#include "../ast.hpp"
#include "compiler.hpp"

namespace yume {
auto TypeWalkVisitor::visit(ast::AST& expr, [[maybe_unused]] const char* label) -> TypeWalkVisitor& {
  if (expr.kind() == ast::TypeNameKind) {
    auto& type_name_expr = dynamic_cast<ast::TypeName&>(expr);
    auto& type = type_name_expr.type();
    type.add_observer(&type_name_expr);
    visit(type, nullptr);
    return *this;
  }
  if (expr.kind() == ast::SimpleTypeKind || expr.kind() == ast::QualTypeKind || expr.kind() == ast::SelfTypeKind) {
    auto& type = dynamic_cast<ast::Type&>(expr);
    auto* resolved_type = &m_compiler.convert_type(type, m_current_fn->parent());
    type.val_ty(resolved_type);
  }
  expr.visit(*this);
  return *this;
}

auto TypeWalkVisitor::visit([[maybe_unused]] std::nullptr_t null, [[maybe_unused]] const char* label)
    -> TypeWalkVisitor& {
  return *this;
}

auto TypeWalkVisitor::visit([[maybe_unused]] const string& str, [[maybe_unused]] const char* label)
    -> TypeWalkVisitor& {
  return *this;
}
} // namespace yume
