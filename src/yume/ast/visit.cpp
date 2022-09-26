#include "ast.hpp"

#include "diagnostic/visitor/visitor.hpp"
#include "ty/compatibility.hpp"
#include "util.hpp"
#include <string>
#include <variant>
#include <vector>

namespace yume::ast {

void IfStmt::visit(Visitor& visitor) const { visitor.visit(clauses).visit(else_clause, "else"); }
void IfClause::visit(Visitor& visitor) const { visitor.visit(cond).visit(body); }
void NumberExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }
void StringExpr::visit(Visitor& visitor) const { visitor.visit(m_val); }
void CharExpr::visit(Visitor& visitor) const { visitor.visit(string{static_cast<char>(m_val)}); }
void BoolExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }
void ReturnStmt::visit(Visitor& visitor) const { visitor.visit(expr); }
void WhileStmt::visit(Visitor& visitor) const { visitor.visit(cond).visit(body); }
void VarDecl::visit(Visitor& visitor) const { visitor.visit(name).visit(type).visit(init); }
void ConstDecl::visit(Visitor& visitor) const { visitor.visit(name).visit(type).visit(init); }
void FnDecl::visit(Visitor& visitor) const {
  visitor.visit(name)
      .visit(args, "arg")
      .visit(type_args, "type arg")
      .visit(annotations, "annotation")
      .visit(ret, "ret");

  if (const auto* s = get_if<string>(&body); s) {
    visitor.visit(*s, "primitive");
  } else if (const auto* s = get_if<extern_decl_t>(&body); s) {
    visitor.visit(s->name, "extern");
    if (s->varargs)
      visitor.visit("varargs");
  } else {
    visitor.visit(get<Compound>(body));
  }
}
void CtorDecl::visit(Visitor& visitor) const { visitor.visit(args, "arg").visit(body); }
void StructDecl::visit(Visitor& visitor) const {
  visitor.visit(name).visit(fields, "field").visit(type_args, "type arg").visit(body);
}
void SimpleType::visit(Visitor& visitor) const { visitor.visit(m_name); }
void QualType::visit(Visitor& visitor) const { visitor.visit(m_base, describe().c_str()); }
void TemplatedType::visit(Visitor& visitor) const { visitor.visit(m_base).visit(m_type_args, "type arg"); }
void FunctionType::visit(Visitor& visitor) const {
  visitor.visit(m_ret).visit(m_args);
  if (m_fn_ptr)
    visitor.visit("fn ptr");
}
void ProxyType::visit(Visitor& visitor) const { visitor.visit(m_field); }
void TypeName::visit(Visitor& visitor) const { visitor.visit(name).visit(type); }
void Compound::visit(Visitor& visitor) const { visitor.visit(body); }
void VarExpr::visit(Visitor& visitor) const { visitor.visit(m_name); }
void ConstExpr::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_parent); }
void CallExpr::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_args); }
void CtorExpr::visit(Visitor& visitor) const { visitor.visit(m_type).visit(m_args); }
void DtorExpr::visit(Visitor& visitor) const { visitor.visit(m_base); }
void SliceExpr::visit(Visitor& visitor) const { visitor.visit(m_type).visit(m_args); }
void LambdaExpr::visit(Visitor& visitor) const {
  visitor.visit(args).visit(annotations, "annotation").visit(ret).visit(body);
}
void DirectCallExpr::visit(Visitor& visitor) const { visitor.visit(base).visit(args); }
void AssignExpr::visit(Visitor& visitor) const { visitor.visit(target).visit(value); }
void FieldAccessExpr::visit(Visitor& visitor) const { visitor.visit(base).visit(field); }
void ImplicitCastExpr::visit(Visitor& visitor) const { visitor.visit(conversion.to_string()).visit(base); }
void Program::visit(Visitor& visitor) const { visitor.visit(body); }

} // namespace yume::ast
