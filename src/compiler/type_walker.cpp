#include "type_walker.hpp"
#include "ast/ast.hpp"
#include "compiler.hpp"
#include "diagnostic/errors.hpp"
#include "type.hpp"
#include "type_holder.hpp"
#include "vals.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

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

template <> void TypeWalker::expression(ast::BoolExpr& expr) { expr.val_ty(m_compiler.m_types.bool_type); }

template <> void TypeWalker::expression(ast::Type& expr) {
  const auto* resolved_type = &convert_type(expr);
  expr.val_ty(resolved_type);
  if (auto* qual_type = dyn_cast<ast::QualType>(&expr))
    expression(qual_type->base());
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
  expression(expr.type());
  const auto& base_type = convert_type(expr.type());
  expr.val_ty(&base_type);
}

template <> void TypeWalker::expression(ast::SliceExpr& expr) {
  for (auto& i : expr.args()) {
    body_expression(i);
  }
  expression(expr.type());
  const auto& base_type = convert_type(expr.type());
  expr.val_ty(&base_type.known_slice());
}

template <> void TypeWalker::expression(ast::AssignExpr& expr) {
  body_expression(expr.target());
  body_expression(expr.value());
  expr.target().attach_to(&expr.value());
}

template <> void TypeWalker::expression(ast::VarExpr& expr) {
  if (!m_scope.contains(expr.name()))
    throw std::runtime_error("Scope doesn't contain variable called "s + expr.name());
  expr.attach_to(m_scope.at(expr.name()));
}

template <> void TypeWalker::expression(ast::FieldAccessExpr& expr) {
  body_expression(expr.base());
  const auto& type = *expr.base().val_ty();

  const auto* struct_type = dyn_cast<ty::Struct>(&type.without_qual());

  if (struct_type == nullptr)
    throw std::runtime_error("Can't access field of expression with non-struct type");

  auto target_name = expr.field();
  const ty::Type* target_type{};
  int j = 0;
  for (const auto& field : struct_type->fields()) {
    if (field.name() == target_name) {
      target_type = field.type().val_ty();
      break;
    }
    j++;
  }

  expr.offset(j);
  expr.val_ty(target_type);
}

template <typename Fn = std::identity>
static auto join_args(const auto& iter, Fn fn = {}, llvm::raw_ostream& stream = errs()) {
  int j = 0;
  for (auto& i : iter) {
    if (j++ != 0)
      stream << ", ";
    stream << fn(i)->name();
  }
}

template <> void TypeWalker::expression(ast::CallExpr& expr) {
  auto fns_by_name = vector<Fn*>();
  auto overloads = vector<std::tuple<uint64_t, Fn*, Instantiation>>();
  auto name = expr.name();

  for (auto& fn : m_compiler.m_fns)
    if (fn.name() == name)
      fns_by_name.push_back(&fn);

  if (fns_by_name.empty())
    throw std::logic_error("No function overload named "s + name);

  vector<const ty::Type*> arg_types{};
  for (auto& i : expr.args()) {
    body_expression(i);
    arg_types.push_back(i.val_ty());
  }

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "\n*** BEGIN OVERLOAD EVALUATION ***\n";
  errs() << "Functions with matching names for call " << name << " with types ";
  join_args(arg_types);
  errs() << "\n";
  for (const auto& i_s : fns_by_name) {
    errs() << "  " << i_s->name() << ":" << i_s->ast().location().begin_line << " with types ";
    join_args(i_s->ast().args(), [](const auto& ast) { return ast.val_ty(); });
    errs() << "\n";
  }
#endif

  for (auto* fn : fns_by_name) {
    uint64_t compat = 0;
    Instantiation instantiation{};
    const auto& fn_ast = fn->m_ast_decl;
    auto fn_arg_size = fn_ast.args().size();

    // The overload is only considered if it has a matching amount of parameters.
    if (arg_types.size() == fn_arg_size || (expr.args().size() >= fn_arg_size && fn_ast.varargs())) {
      // Determine the type compatibility of each argument individually.
      unsigned i = 0;
      for (const auto& arg_type : arg_types) {
        // Don't try to determine type compatibility of the "var" part of varargs functions.
        // Currently, varargs methods can only be primitives and carry no type information for their variadic part. This
        // will change in the future.
        if (i >= fn_arg_size)
          break;

        const auto* ast_arg = fn_ast.args()[i].val_ty();
        auto [i_compat, gen, gen_sub] = arg_type->compatibility(*ast_arg);
        // One incompatible parameter disqualifies the function entirely
        if (i_compat == ty::Type::Compatiblity::INVALID) {
          compat = i_compat;
          break;
        }

        // The function compatibility determined a substitution for a type variable.
        if (gen != nullptr && gen_sub != nullptr) {
          auto existing = instantiation.m_sub.find(gen);
          // The substitution must have an intersection with an already deduced value for the same type variable
          if (existing != instantiation.m_sub.end()) {
            const auto* intersection = gen_sub->intersect(*existing->second);
            if (intersection == nullptr) {
              // The types don't have a common intersection, they cannot coexist.
              compat = ty::Type::Compatiblity::INVALID;
              break;
            }
            instantiation.m_sub[gen] = intersection;
          } else {
            instantiation.m_sub.try_emplace(gen, gen_sub);
          }
        }
        compat += i_compat;
        i++;
      }
      if (compat != ty::Type::Compatiblity::INVALID)
        overloads.emplace_back(compat, fn, instantiation);
    }
  }

#ifdef YUME_SPEW_OVERLOAD_SELECTION
  errs() << "Available overloads for call " << name << " with types ";
  join_args(arg_types);
  errs() << "\n";
  for (const auto& [i_w, i_s, i_i] : overloads) {
    errs() << "  Weight " << i_w << " " << i_s->name() << ":" << i_s->ast().location().begin_line << " with types ";

    join_args(i_s->ast().args(), [](const auto& ast) { return ast.val_ty(); });
    errs() << " where";
    for (const auto& [sub_k, sub_v] : i_i.m_sub) {
      errs() << " " << sub_k->name() << " = " << sub_v->name();
    }
    errs() << "\n";
  }
  errs() << "\n*** END OVERLOAD EVALUATION ***\n\n";
#endif

  if (overloads.empty()) {
    string str{};
    raw_string_ostream ss{str};
    ss << "No matching overload for " << name << " with argument types ";
    join_args(arg_types, {}, ss);
    throw std::logic_error(str);
  }

  auto [selected_weight, selected, instantiate] =
      *std::max_element(overloads.begin(), overloads.end(),
                        [&](const auto& a, const auto& b) { return get<uint64_t>(a) < get<uint64_t>(b); });

  // It is an instantiation of a function template
  if (!instantiate.m_sub.empty()) {
    // TODO: move most of this logic directly into Fn
    // Try to find an already existing instantiation with the same substitutions
    auto existing_instantiation = m_current_fn->m_instantiations.find(instantiate);
    if (existing_instantiation == m_current_fn->m_instantiations.end()) {
      // An existing one wasn't found. Duplicate the template's AST with the substituted types.
      auto* decl_clone = selected->m_ast_decl.clone();
      m_current_fn->m_member->direct_body().emplace_back(decl_clone);

      std::map<string, const ty::Type*> subs{};
      for (const auto& [k, v] : instantiate.m_sub)
        subs.try_emplace(k->name(), v);

      auto fn_ptr = std::make_unique<Fn>(*decl_clone, selected->m_parent, selected->m_member, move(subs));
      auto new_emplace = m_current_fn->m_instantiations.emplace(instantiate, move(fn_ptr));
      auto& new_fn = *new_emplace.first->second;

      // TODO: Push this in a queue to not cause a huge stack trace when an instantiation causes another instantiation
      // TODO: This really needs to go into a queue because an instantiation can loop back to requiring to instantiate
      // itself while the first instantiation hasn't finished existing this scope yet, leading to god knows what
      // outcome.

      // Save everything pertaining to the old context
      auto saved_scope = m_scope;
      auto* saved_current = m_current_fn;

      // Temporarily create a new context
      m_in_depth = false;
      m_scope.clear();
      m_current_fn = &new_fn;

      // Type-annotate the cloned declaration
      body_statement(*decl_clone);
      m_in_depth = true;

      auto* llvm_fn = m_compiler.declare(new_fn);
      new_fn.m_llvm_fn = llvm_fn;
      selected = &new_fn;

      // Restore again
      m_scope = saved_scope;
      m_current_fn = saved_current;
    } else {
      selected = existing_instantiation->second.get();
    }
  }

  for (auto [target, expr_arg] : llvm::zip(selected->ast().args(), expr.direct_args())) {
    const auto* target_type = target.type().val_ty();
    if (expr_arg->val_ty() == target_type)
      continue;

    auto cast_expr = std::make_unique<ast::ImplicitCastExpr>(expr_arg->token_range(), std::move(expr_arg), target_type);
    cast_expr->val_ty(target_type);
    expr_arg = std::move(cast_expr);
  }

  if (selected->ast().ret().has_value())
    expr.val_ty(selected->ast().ret()->get().val_ty());

  expr.selected_overload(selected);
}

template <> void TypeWalker::statement(ast::Compound& stat) {
  for (auto& i : stat.body())
    body_statement(i);
}

template <> void TypeWalker::statement(ast::StructDecl& stat) {
  for (auto& i : stat.fields()) {
    expression(i);
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
    stat.attach_to(&stat.ret()->get());
  }

  // This decl still has unsubstituted generics, can't instantiate its body
  if (std::any_of(m_current_fn->m_subs.begin(), m_current_fn->m_subs.end(),
                  [&](const auto& sub) { return isa<ty::Generic>(sub.second); })) {
    return;
  }

  if (m_in_depth && stat.body().index() == 0)
    statement(get<0>(stat.body()));
}

template <> void TypeWalker::statement(ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    body_expression(stat.expr()->get());
    m_current_fn->m_ast_decl.attach_to(&stat.expr()->get());
  }
}

template <> void TypeWalker::statement(ast::VarDecl& stat) {
  body_expression(stat.init());
  if (stat.type().has_value()) {
    expression(stat.type()->get());
    stat.init().attach_to(&stat.type()->get());
  }

  stat.val_ty(&stat.init().val_ty()->known_mut());
  m_scope.insert({stat.name(), &stat});
}

template <> void TypeWalker::statement(ast::IfStmt& stat) {
  for (auto& i : stat.clauses()) {
    body_expression(i.cond());
    statement(i.body());
  }
  if (stat.else_clause().has_value())
    statement(*stat.else_clause());
}

template <> void TypeWalker::statement(ast::WhileStmt& stat) {
  body_expression(stat.cond());
  statement(stat.body());
}

void TypeWalker::body_statement(ast::Stmt& stat) {
  const ASTStackTrace guard("Semantic: "s + stat.kind_name() + " statement", stat);
  return CRTPWalker::body_statement(stat);
};
void TypeWalker::body_expression(ast::Expr& expr) {
  const ASTStackTrace guard("Semantic: "s + expr.kind_name() + " expression", expr);
  return CRTPWalker::body_expression(expr);
};

auto TypeWalker::convert_type(const ast::Type& ast_type) -> const ty::Type& {
  const ty::Type* parent = m_current_fn == nullptr ? m_current_struct : m_current_fn->parent();
  Fn* context = m_current_fn;

  if (const auto* simple_type = dyn_cast<ast::SimpleType>(&ast_type)) {
    auto name = simple_type->name();
    if (context != nullptr) {
      auto generic = context->m_subs.find(name);
      if (generic != context->m_subs.end())
        return *generic->second;
    }
    auto val = m_compiler.m_types.known.find(name);
    if (val != m_compiler.m_types.known.end())
      return *val->second;
  } else if (const auto* qual_type = dyn_cast<ast::QualType>(&ast_type)) {
    auto qualifier = qual_type->qualifier();
    return convert_type(qual_type->base()).known_qual(qualifier);
  } else if (isa<ast::SelfType>(ast_type)) {
    if (parent != nullptr)
      return *parent;
  }

  throw std::runtime_error("Cannot convert AST type to actual type!");
};
} // namespace yume
