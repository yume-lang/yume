//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_COMPILER_HPP
#define YUME_CPP_COMPILER_HPP

#include "../ast.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include "type_holder.hpp"
#include "vals.hpp"
#include <llvm/ADT/iterator_range.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>
namespace llvm {
class Function;
class Type;
} // namespace llvm

namespace yume {
struct TypeWalkVisitor;
using namespace llvm;

class Compiler {
  std::vector<SourceFile> m_sources;
  TypeHolder m_types;
  vector<Fn> m_fns{};
  std::queue<Fn*> m_decl_queue{};

  Fn* m_current_fn{};
  std::map<string, Val> m_scope{};

  unique_ptr<LLVMContext> m_context;
  unique_ptr<IRBuilder<>> m_builder;
  unique_ptr<Module> m_module;
  unique_ptr<TargetMachine> m_targetMachine;

  friend TypeWalkVisitor;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }

  explicit Compiler(std::vector<SourceFile> source_files);

  [[nodiscard]] auto declare(Fn&, bool mangle = true) -> llvm::Function*;

  void define(Fn&);

  void body_statement(const ast::Stmt&);
  void decl_statement(ast::Stmt&, ty::Type* parent = nullptr);
  auto body_expression(const ast::Expr&, bool mut = false) -> Val;

  void write_object(const char* filename, bool binary);

  auto convert_type(const ast::Type& ast_type, ty::Type* parent = nullptr) -> ty::Type&;

  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  auto mangle_name(const ast::FnDecl& fn_decl, ty::Type* parent = nullptr) -> string;

  auto mangle_name(const ast::Type& ast_type, ty::Type* parent = nullptr) -> string;

  [[nodiscard]] inline auto source_files() -> const auto& { return m_sources; }

private:
  template <typename T>
  requires std::conjunction_v<std::is_base_of<ast::Stmt, T>, std::negation<std::is_base_of<ast::Expr, T>>>
  void conv_statement(const ast::Stmt& stat) {
    return statement<T>(dynamic_cast<const T&>(stat));
  }

  template <typename T>
  requires std::conjunction_v<std::is_base_of<ast::Stmt, T>, std::negation<std::is_base_of<ast::Expr, T>>>
  void statement(const T& stat);

  template <typename T>
  requires std::is_base_of_v<ast::Expr, T>
  auto conv_expression(const ast::Expr& expr, bool mut = false) -> Val {
    return expression<T>(dynamic_cast<const T&>(expr), mut);
  }

  template <typename T>
  requires std::is_base_of_v<ast::Expr, T>
  auto expression(const T&, bool mut = false) -> Val;

  auto known_type(const string& str) -> ty::Type&;

  void walk_types();
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
