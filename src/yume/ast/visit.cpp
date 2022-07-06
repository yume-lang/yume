#include "ast.hpp"

#include "diagnostic/visitor/visitor.hpp"
#include "ty/compatibility.hpp"
#include "util.hpp"
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace yume::ast {

void IfStmt::visit(Visitor& visitor) const { visitor.visit(m_clauses).visit(m_else_clause, "else"); }
void IfClause::visit(Visitor& visitor) const { visitor.visit(m_cond).visit(m_body); }
void NumberExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }
void StringExpr::visit(Visitor& visitor) const { visitor.visit(m_val); }
void CharExpr::visit(Visitor& visitor) const { visitor.visit(string{static_cast<char>(m_val)}); }
void BoolExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }
void ReturnStmt::visit(Visitor& visitor) const { visitor.visit(m_expr); }
void WhileStmt::visit(Visitor& visitor) const { visitor.visit(m_cond).visit(m_body); }
void VarDecl::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_type).visit(m_init); }
void FnDecl::visit(Visitor& visitor) const {
  visitor.visit(m_name).visit(m_args, "arg").visit(m_type_args, "type arg").visit(m_ret, "ret");
  if (const auto* s = get_if<string>(&m_body); s)
    visitor.visit(*s, "primitive");
  else
    visitor.visit(get<Compound>(m_body));

  if (m_varargs)
    visitor.visit("varargs");
}
void StructDecl::visit(Visitor& visitor) const {
  visitor.visit(m_name).visit(m_fields, "field").visit(m_type_args, "type arg").visit(m_body);
}
void SimpleType::visit(Visitor& visitor) const { visitor.visit(m_name); }
void QualType::visit(Visitor& visitor) const { visitor.visit(m_base, describe().c_str()); }
void TemplatedType::visit(Visitor& visitor) const { visitor.visit(m_base).visit(m_type_args, "type arg"); }
void TypeName::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_type); }
void Compound::visit(Visitor& visitor) const { visitor.visit(m_body); }
void VarExpr::visit(Visitor& visitor) const { visitor.visit(m_name); }
void CallExpr::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_args); }
void CtorExpr::visit(Visitor& visitor) const { visitor.visit(m_type).visit(m_args); }
void DtorExpr::visit(Visitor& visitor) const { visitor.visit(m_base); }
void SliceExpr::visit(Visitor& visitor) const { visitor.visit(m_type).visit(m_args); }
void AssignExpr::visit(Visitor& visitor) const { visitor.visit(m_target).visit(m_value); }
void FieldAccessExpr::visit(Visitor& visitor) const { visitor.visit(m_base).visit(m_field); }
void ImplicitCastExpr::visit(Visitor& visitor) const { visitor.visit(m_conversion.to_string()).visit(m_base); }
void Program::visit(Visitor& visitor) const { visitor.visit(m_body); }

} // namespace yume::ast
