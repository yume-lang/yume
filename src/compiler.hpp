//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_COMPILER_HPP
#define YUME_CPP_COMPILER_HPP

#include "ast.hpp"
#include "type.hpp"
#include "util.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <map>

namespace yume {
using namespace llvm;

struct Fn {
  const ast::FnDeclStatement& m_ast_decl;
  llvm::Function* m_llvm_fn;
  std::map<string, llvm::Value*> m_scope{};

  inline Fn(const ast::FnDeclStatement& ast_decl, llvm::Function* llvm_fn) : m_ast_decl(ast_decl), m_llvm_fn(llvm_fn) {}

  [[nodiscard]] auto inline body() const -> const auto& { return m_ast_decl.body(); };

  operator llvm::Function*() const { // NOLINT(google-explicit-constructor)
    return m_llvm_fn;
  }
};

class Compiler {
  unique_ptr<ast::Program> m_program;
  std::map<string, unique_ptr<ty::Type>> m_known_types{};
  vector<unique_ptr<Fn>> m_fn_decls{};

  Fn* m_current_fn{};

  unique_ptr<LLVMContext> m_context;
  unique_ptr<IRBuilder<>> m_builder;
  unique_ptr<Module> m_module;
  unique_ptr<TargetMachine> m_targetMachine;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }

  explicit Compiler(unique_ptr<ast::Program> program);

  auto declare(const ast::FnDeclStatement& fn_decl) -> Function*;

  void define(Fn&);

  void body_statement(const ast::Statement&);
  auto body_expression(const ast::Expr&) -> llvm::Value*;

  void write_object(const char* filename, bool binary);

  auto convert_type(const ast::Type& ast_type) -> ty::Type&;

  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  auto mangle_name(const ast::FnDeclStatement& fn_decl) -> string;

  auto mangle_name(const ast::Type& ast_type) -> string;

protected:
  void statement(const ast::Compound&);
  void statement(const ast::WhileStatement&);
  void statement(const ast::IfStatement&);
  void statement(const ast::ReturnStatement&);
  void statement(const ast::ExprStatement&);
  void statement(const ast::VarDeclStatement&);

  auto expression(const ast::NumberExpr&) -> llvm::Value*;
  auto expression(const ast::VarExpr&) -> llvm::Value*;
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
