#include "type_walker.hpp"
#include "../ast.hpp"
#include "compiler.hpp"

namespace yume {

template <> void TypeWalkVisitor::expression(const ast::NumberExpr& expr) {
    auto val = expr.val();
    if (val > std::numeric_limits<int32_t>::max()) {
      expr.val_ty(m_compiler.m_types.int64().s_ty);
    } else {
      expr.val_ty(m_compiler.m_types.int32().s_ty);
    }
}

/*
void TypeWalkVisitor::expression(ast::AST& expr, [[maybe_unused]] const char* label) {
  if (!m_in_depth && expr.kind() == ast::CompoundKind) {
    return *this; // Skip (first pass)
  }
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
  if (expr.kind() == ast::StringKind) {
    // TODO: String type
    expr.val_ty(&m_compiler.m_types.int8().u_ty->known_ptr());
    return *this;
  }
  if (expr.kind() == ast::CharKind) {
    expr.val_ty(m_compiler.m_types.int8().u_ty);
    return *this;
  }
  if (expr.kind() == ast::CtorKind) {
    auto& ctor = dynamic_cast<ast::CtorExpr&>(expr);
    ctor.val_ty(ctor.name() == "self" ? m_current_fn->parent() : &m_compiler.known_type(ctor.name()));
  }
  if (expr.kind() == ast::AssignKind) {
    auto& assign = dynamic_cast<ast::AssignExpr&>(expr);
    assign.value().add_observer(&assign.target());
  }
  expr.visit(*this);
  return *this;
}
*/

} // namespace yume
