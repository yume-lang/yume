#include "type_walker.hpp"
#include "../ast.hpp"
#include "compiler.hpp"

namespace yume {

template <> void TypeWalker::expression(ast::NumberExpr& expr) {
  auto val = expr.val();
  if (val > std::numeric_limits<int32_t>::max()) {
    expr.val_ty(m_compiler.m_types.int64().s_ty);
  } else {
    expr.val_ty(m_compiler.m_types.int32().s_ty);
  }
}

template <> void TypeWalker::expression(ast::StringExpr& expr) {
  // TODO: String type
  expr.val_ty(&m_compiler.m_types.int8().u_ty->known_ptr());
}

template <> void TypeWalker::expression(ast::CharExpr& expr) { expr.val_ty(m_compiler.m_types.int8().u_ty); }

template <> void TypeWalker::expression(ast::Type& expr) {
  auto* resolved_type = &m_compiler.convert_type(expr, m_current_fn->parent());
  expr.val_ty(resolved_type);
  if (expr.kind() == ast::QualTypeKind) {
    expression(dynamic_cast<ast::QualType&>(expr).base());
  }
}

template <> void TypeWalker::expression(ast::TypeName& expr) {
  auto& type = expr.type();
  expr.attach_to(&type);
  expression(type);
}

template <> void TypeWalker::expression(ast::CtorExpr& expr) {
  for (auto& i : expr.args()) {
    body_expression(i);
  }
  expr.val_ty(expr.name() == "self" ? m_current_fn->parent() : &m_compiler.known_type(expr.name()));
}

template <> void TypeWalker::expression(ast::AssignExpr& expr) {
  body_expression(expr.target());
  body_expression(expr.value());
  expr.target().attach_to(&expr.value());
}

template <> void TypeWalker::expression(ast::VarExpr& expr) {
  if (!m_scope.contains(expr.name())) {
    return; // TODO: this should be an error
  }
  expr.attach_to(m_scope.at(expr.name()));
}

template <> void TypeWalker::expression(ast::FieldAccessExpr& expr) {
  body_expression(expr.base());
  auto& type = *expr.base().val_ty();

  ty::StructType* struct_type = nullptr;

  if (type.kind() == ty::Kind::Struct) {
    struct_type = &dynamic_cast<ty::StructType&>(type);
  } else if (type.is_mut()) {
    if (auto* base = type.qual_base(); base->kind() == ty::Kind::Struct) {
      struct_type = &dynamic_cast<ty::StructType&>(*base);
    }
  }

  if (struct_type == nullptr) {
    throw std::runtime_error("Can't access field of expression with non-struct type");
  }

  auto target_name = expr.field();
  ty::Type* target_type{};
  for (const auto& field : struct_type->fields()) {
    if (field.name() == target_name) {
      // TODO: struct fields should also go through the type walker so they have attached types
      target_type = &m_compiler.convert_type(field.type(), struct_type);
      break;
    }
  }

  expr.val_ty(target_type);
}

template <> void TypeWalker::expression(ast::CallExpr& expr) {
  auto fns_by_name = vector<Fn*>();
  auto overloads = vector<std::pair<int, Fn*>>();
  auto name = expr.name();

  for (auto& fn : m_compiler.m_fns) {
    if (fn.name() == name) {
      fns_by_name.push_back(&fn);
    }
  }
  if (fns_by_name.empty()) {
    throw std::logic_error("No function overload named "s + name);
  }

  vector<ty::Type*> arg_types{};
  for (auto& i : expr.args()) {
    body_expression(i);
    arg_types.push_back(i.val_ty());
  }

  for (auto* fn : fns_by_name) {
    int compat = 0;
    const auto& fn_ast = fn->m_ast_decl;
    auto fn_arg_size = fn_ast.args().size();
    if (arg_types.size() == fn_arg_size || (expr.args().size() >= fn_arg_size && fn_ast.varargs())) {
      unsigned i = 0;
      for (const auto& arg_type : arg_types) {
        if (i >= fn_arg_size) {
          break;
        }
        auto i_compat = arg_type->compatibility(*fn_ast.args()[i].val_ty());
        if (i_compat == 0) {
          compat = INT_MIN;
          break;
        }
        compat += i_compat;
        i++;
      }
      overloads.emplace_back(compat, fn);
    }
  }

  auto [selected_weight, selected] = *std::max_element(overloads.begin(), overloads.end());
  if (selected_weight < 0) {
    throw std::logic_error("No viable overload for "s + name);
  }

  if (selected->m_ast_decl.ret().has_value()) {
    expr.val_ty(selected->ast().ret()->get().val_ty());
  }

  m_current_fn->selected_overload.insert({&expr, selected});
}

template <> void TypeWalker::statement(ast::Compound& stat) {
  for (auto& i : stat.body()) {
    body_statement(i);
  }
}

template <> void TypeWalker::statement(ast::FnDecl& stat) {
  m_scope.clear();
  for (auto& i : stat.args()) {
    expression(i);
    m_scope.insert({i.name(), &i});
  }

  if (stat.ret().has_value()) {
    expression(stat.ret()->get());
    stat.ret()->get().attach_to(&stat);
  }

  // Completely skip fn decls with incomplete types (generics)
  // TODO: properly handle
  if (!stat.type_args().empty()) {
    return;
  }

  if (m_in_depth && stat.body().index() == 0) {
    statement(*get<0>(stat.body()));
  }
}

template <> void TypeWalker::statement(ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    body_expression(stat.expr()->get());
    stat.expr()->get().attach_to(&m_current_fn->m_ast_decl);
  }
}

template <> void TypeWalker::statement(ast::VarDecl& stat) {
  body_expression(stat.init());
  if (stat.type().has_value()) {
    expression(stat.type()->get());
    stat.val_ty(&stat.type()->get().val_ty()->known_mut());
  }

  stat.init().attach_to(&stat);
  m_scope.insert({stat.name(), &stat});
}

auto TypeWalker::visit(ast::AST& expr, [[maybe_unused]] const char* label) -> TypeWalker& {
#ifdef YUME_TYPE_WALKER_FALLBACK_VISITOR
  // nasty RTTI
  if (auto* stmt = dynamic_cast<ast::Stmt*>(&expr); stmt != nullptr) {
    body_statement(*stmt);
  }
#endif
  return *this;
}

void TypeWalker::body_statement(ast::Stmt& stat) { return CRTPWalker::body_statement(stat); };
void TypeWalker::body_expression(ast::Expr& expr) { return CRTPWalker::body_expression(expr); };

} // namespace yume
