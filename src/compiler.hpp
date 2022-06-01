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
#include <queue>
#include <utility>

namespace yume {
using namespace llvm;

class Compiler;

struct Fn {
  const ast::FnDecl& m_ast_decl;
  // TODO: multiple instantiations
  llvm::Function* m_llvm_fn{};

  inline explicit Fn(const ast::FnDecl& ast_decl) : m_ast_decl(ast_decl) {}

  [[nodiscard]] auto inline body() const -> const auto& { return m_ast_decl.body(); }

  [[nodiscard]] auto inline llvm() const -> llvm::Function* { return m_llvm_fn; }

  [[nodiscard]] auto declaration(Compiler& compiler, bool mangle = true) -> llvm::Function*;

  operator llvm::Function*() const { // NOLINT(google-explicit-constructor)
    return m_llvm_fn;
  }
};

struct Val {
  llvm::Value* m_llvm_val{};
  ty::Type* m_type{};

  inline Val(llvm::Value* llvm_val) : m_llvm_val(llvm_val) {} // NOLINT(google-explicit-constructor)
  inline Val(llvm::Value* llvm_val, ty::Type* type) : m_llvm_val(llvm_val), m_type(type) {}
  Val(const Val&) noexcept = default;
  Val(Val&&) noexcept = default;
  auto operator=(const Val&) noexcept -> Val& = default;
  auto operator=(Val&&) noexcept -> Val& = default;
  virtual ~Val() = default;

  [[nodiscard]] auto inline llvm() const -> llvm::Value* { return m_llvm_val; }
  [[nodiscard]] auto inline type() const -> ty::Type* { return m_type; }

  operator llvm::Value*() const { // NOLINT(google-explicit-constructor)
    return m_llvm_val;
  }
};

struct SourceFile {
  const string m_name;
  vector<yume::Token> m_tokens;
  ast::TokenIterator m_iterator;
  unique_ptr<ast::Program> m_program;

  explicit inline SourceFile(std::istream& in, string name)
      : m_name(std::move(name)), m_tokens(yume::tokenize(in, m_name)), m_iterator{m_tokens.begin(), m_tokens.end()} {
#ifdef YUME_SPEW_LIST_TOKENS
    std::cout << "tokens:\n";
    for (auto& i : m_tokens) {
      std::cout << "  " << i << "\n";
    }
    std::cout << "\n";
    std::cout.flush();
#endif
    m_program = ast::Program::parse(m_iterator);
  }
};

class Compiler {
  std::vector<SourceFile> m_sources;
  std::map<string, unique_ptr<ty::Type>> m_known_types{};
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
  auto body_expression(const ast::Expr&) -> Val;

  void write_object(const char* filename, bool binary);

  auto convert_type(const ast::Type& ast_type) -> ty::Type&;

  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  auto mangle_name(const ast::FnDecl& fn_decl) -> string;

  auto mangle_name(const ast::Type& ast_type) -> string;

protected:
  void statement(const ast::Compound&);
  void statement(const ast::WhileStmt&);
  void statement(const ast::IfStmt&);
  void statement(const ast::ReturnStmt&);
  void statement(const ast::VarDecl&);

  auto expression(const ast::NumberExpr&) -> Val;
  auto expression(const ast::StringExpr&) -> Val;
  auto expression(const ast::CharExpr&) -> Val;
  auto expression(const ast::VarExpr&) -> Val;
  auto expression(const ast::CallExpr&) -> Val;
  auto expression(const ast::AssignExpr&) -> Val;

  inline auto known_type(const string& str) -> ty::Type& { return *m_known_types.at(str); }
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
