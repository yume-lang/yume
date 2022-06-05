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
#if __has_include(<llvm/MC/TargetRegistry.h>)
#include <llvm/MC/TargetRegistry.h>
#else
#include <llvm/Support/TargetRegistry.h>
#endif
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
  ty::Type* m_parent;
  // TODO: multiple instantiations
  llvm::Function* m_llvm_fn{};
  vector<ty::Type*> m_arg_types{};
  ty::Type* m_ret_type{};

  [[nodiscard]] auto inline body() const -> const auto& { return m_ast_decl.body(); }

  [[nodiscard]] auto inline name() const { return m_ast_decl.name(); }

  [[nodiscard]] auto inline ast() const -> const auto& { return m_ast_decl; }

  [[nodiscard]] auto inline llvm() const -> llvm::Function* { return m_llvm_fn; }

  [[nodiscard]] auto inline parent() const -> ty::Type* { return m_parent; }

  [[nodiscard]] auto inline arg_types() const { return dereference_view(m_arg_types); }

  [[nodiscard]] auto inline ret_type() const { return m_ret_type; }

  [[nodiscard]] auto declaration(Compiler& compiler, bool mangle = true) -> llvm::Function*;

  operator llvm::Function*() const { // NOLINT(google-explicit-constructor)
    return m_llvm_fn;
  }
};

struct Val {
  llvm::Value* m_llvm_val{};
  ty::Type* m_type{};

  explicit inline Val(llvm::Value* llvm_val) : m_llvm_val(llvm_val) {}
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

struct TypeHolder {
  struct IntTypePair {
    ty::IntegerType* signed_ty;
    ty::IntegerType* unsigned_ty;
  };

  std::array<IntTypePair, 4> int_types{};
  ty::UnknownType unknown{};
  std::map<string, unique_ptr<ty::Type>> known{};

  TypeHolder();

  inline constexpr auto int8() -> IntTypePair { return int_types[0]; }
  inline constexpr auto int16() -> IntTypePair { return int_types[1]; }
  inline constexpr auto int32() -> IntTypePair { return int_types[2]; }
  inline constexpr auto int64() -> IntTypePair { return int_types[3]; }
};

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
  void decl_statement(const ast::Stmt&, ty::Type* parent = nullptr);
  auto body_expression(const ast::Expr&, bool mut = false) -> Val;

  void write_object(const char* filename, bool binary);

  auto convert_type(const ast::Type& ast_type, ty::Type* parent = nullptr) -> ty::Type&;

  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  auto mangle_name(const ast::FnDecl& fn_decl, ty::Type* parent = nullptr) -> string;

  auto mangle_name(const ast::Type& ast_type, ty::Type* parent = nullptr) -> string;

protected:
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

  inline auto known_type(const string& str) -> ty::Type& { return *m_types.known.at(str); }
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
