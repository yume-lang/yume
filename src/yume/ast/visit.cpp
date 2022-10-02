#include "ast.hpp"

#include "diagnostic/visitor/visitor.hpp"
#include "ty/compatibility.hpp"
#include "util.hpp"
#include <string>
#include <variant>
#include <vector>

namespace yume::ast {

namespace {

struct VisitorHelper {
  Visitor& visitor;

  auto visit(const ast::AST& value, string_view label) -> auto& { return visitor.visit(value, label), *this; }
  auto visit(std::nullptr_t value, string_view label) -> auto& { return visitor.visit(value, label), *this; }
  auto visit(const string& value, string_view label) -> auto& { return visitor.visit(value, label), *this; }

  template <typename T> auto visit(const ast::OptionalAnyBase<T>& any_base, string_view label) -> auto& {
    if (any_base.raw_ptr() != nullptr)
      return visit(*any_base, label);
    return visit(nullptr, label);
  }

  template <typename T> auto visit(const ast::AnyBase<T>& any_base, string_view label) -> auto& {
    yume_assert(any_base.raw_ptr() != nullptr, "AnyBase should never be null");
    return visit(*any_base, label);
  }

  template <std::ranges::range Range>
  requires (!std::convertible_to<Range, const char*>)
  auto visit(const Range& iter, string_view label) -> auto& {
    for (auto& i : iter)
      visit(i, label);
    return *this;
  }

  template <typename T> auto visit(const optional<T>& opt, string_view label) -> auto& {
    if (opt.has_value())
      visit(*opt, label);
    return *this;
  }

  template <typename... Ts> [[deprecated]] auto visit(const std::variant<Ts...>& var, string_view label) -> auto& {
    std::visit([this, label](auto&& x) { visit(std::forward<decltype(x)>(x), label); }, var);
    return *this;
  }

  template <typename T> auto visit(std::pair<T, string_view>& pair) -> auto& { return visit(pair.first, pair.second); }

  auto visit(std::same_as<bool> auto value, string_view label) -> auto& {
    return visit(value ? "true" : "false", label);
  }
};

auto helper(Visitor& visitor) { return VisitorHelper{visitor}; }
} // namespace

void IfStmt::visit(Visitor& visitor) const { helper(visitor).visit(clauses, "clause").visit(else_clause, "else"); }
void IfClause::visit(Visitor& visitor) const { helper(visitor).visit(cond, "cond").visit(body, "body"); }
void NumberExpr::visit(Visitor& visitor) const { helper(visitor).visit(describe(), "value"); }
void StringExpr::visit(Visitor& visitor) const { helper(visitor).visit(val, "value"); }
void CharExpr::visit(Visitor& visitor) const { helper(visitor).visit(string{static_cast<char>(val)}, "value"); }
void BoolExpr::visit(Visitor& visitor) const { helper(visitor).visit(describe(), "value"); }
void ReturnStmt::visit(Visitor& visitor) const { helper(visitor).visit(expr, "expr"); }
void WhileStmt::visit(Visitor& visitor) const { helper(visitor).visit(cond, "cond").visit(body, "body"); }
void VarDecl::visit(Visitor& visitor) const {
  helper(visitor).visit(name, "name").visit(type, "type").visit(init, "init");
}
void ConstDecl::visit(Visitor& visitor) const {
  helper(visitor).visit(name, "name").visit(type, "type").visit(init, "init");
}
void FnDecl::visit(Visitor& visitor) const {
  auto vis = helper(visitor)
                 .visit(name, "name")
                 .visit(args, "arg")
                 .visit(type_args, "type arg")
                 .visit(annotations, "annotation")
                 .visit(ret, "ret");

  if (const auto* s = get_if<string>(&body); s) {
    vis.visit(*s, "primitive");
  } else if (const auto* s = get_if<extern_decl_t>(&body); s) {
    vis.visit(s->name, "extern");
    if (s->varargs)
      vis.visit(true, "varags");
  } else {
    vis.visit(get<Compound>(body), "body");
  }
}
void CtorDecl::visit(Visitor& visitor) const { helper(visitor).visit(args, "arg").visit(body, "body"); }
void StructDecl::visit(Visitor& visitor) const {
  helper(visitor).visit(name, "name").visit(fields, "field").visit(type_args, "type arg").visit(body, "body");
}
void SimpleType::visit(Visitor& visitor) const { helper(visitor).visit(name, "name"); }
void QualType::visit(Visitor& visitor) const { helper(visitor).visit(base, describe().c_str()); }
void TemplatedType::visit(Visitor& visitor) const { helper(visitor).visit(base, "base").visit(type_args, "type arg"); }
void FunctionType::visit(Visitor& visitor) const {
  auto vis = helper(visitor).visit(ret, "ret").visit(args, "args");
  if (fn_ptr)
    vis.visit(true, "fn ptr");
}
void ProxyType::visit(Visitor& visitor) const { helper(visitor).visit(field, "field"); }
void TypeName::visit(Visitor& visitor) const { helper(visitor).visit(name, "name").visit(type, "type"); }
void Compound::visit(Visitor& visitor) const { helper(visitor).visit(body, "body"); }
void VarExpr::visit(Visitor& visitor) const { helper(visitor).visit(name, "name"); }
void ConstExpr::visit(Visitor& visitor) const { helper(visitor).visit(name, "name").visit(parent, "name"); }
void CallExpr::visit(Visitor& visitor) const {
  helper(visitor).visit(name, "name").visit(receiver, "receiver").visit(args, "args");
}
void CtorExpr::visit(Visitor& visitor) const { helper(visitor).visit(type, "type").visit(args, "args"); }
void DtorExpr::visit(Visitor& visitor) const { helper(visitor).visit(base, "base"); }
void SliceExpr::visit(Visitor& visitor) const { helper(visitor).visit(type, "type").visit(args, "args"); }
void LambdaExpr::visit(Visitor& visitor) const {
  helper(visitor).visit(args, "args").visit(annotations, "annotation").visit(ret, "ret").visit(body, "body");
}
void AssignExpr::visit(Visitor& visitor) const { helper(visitor).visit(target, "target").visit(value, "value"); }
void FieldAccessExpr::visit(Visitor& visitor) const { helper(visitor).visit(base, "base").visit(field, "field"); }
void ImplicitCastExpr::visit(Visitor& visitor) const {
  helper(visitor).visit(conversion.to_string(), "conversion").visit(base, "base");
}
void Program::visit(Visitor& visitor) const { helper(visitor).visit(body, "body"); }

} // namespace yume::ast
