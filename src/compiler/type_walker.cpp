#include "type_walker.hpp"
#include "../ast.hpp"
#include "compiler.hpp"

namespace yume {
static auto observe_type(TypeWalkVisitor& self, ast::Type& type) {
  if (type.kind() == ast::QualTypeKind) {
    self.visit(dynamic_cast<ast::QualType&>(type).base(), nullptr);
  }
  auto* resolved_type = &self.m_compiler.convert_type(type, self.m_current_fn->parent());
  type.val_ty(resolved_type);
}

auto TypeWalkVisitor::visit(ast::AST& expr, [[maybe_unused]] const char* label) -> TypeWalkVisitor& {
  if (expr.kind() == ast::TypeNameKind) {
    auto& type_name_expr = dynamic_cast<ast::TypeName&>(expr);
    auto& type = type_name_expr.type();
    type.add_observer(&type_name_expr);
    observe_type(*this, type);
    return *this;
  }
  if (expr.kind() == ast::SimpleTypeKind || expr.kind() == ast::QualTypeKind || expr.kind() == ast::SelfTypeKind) {
    observe_type(*this, dynamic_cast<ast::Type&>(expr));
    return *this;
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
