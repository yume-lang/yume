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
  void statement(const ast::Compound&);
  void statement(const ast::WhileStmt&);
  void statement(const ast::IfStmt&);
  void statement(const ast::ReturnStmt&);
  void statement(const ast::VarDecl&);

  auto expression(const ast::NumberExpr&, bool mut = false) -> Val;
  auto expression(const ast::StringExpr&, bool mut = false) -> Val;
  auto expression(const ast::CharExpr&, bool mut = false) -> Val;
  auto expression(const ast::VarExpr&, bool mut = false) -> Val;
  auto expression(const ast::CallExpr&, bool mut = false) -> Val;
  auto expression(const ast::AssignExpr&, bool mut = false) -> Val;
  auto expression(const ast::CtorExpr&, bool mut = false) -> Val;
  auto expression(const ast::FieldAccessExpr&, bool mut = false) -> Val;

  auto known_type(const string& str) -> ty::Type&;

  void walk_types();
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
