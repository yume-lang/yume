#pragma once

#include "ast/crtp_walker.hpp"
#include "semantic/type_walker.hpp"
#include "type_holder.hpp"
#include "util.hpp"
#include "vals.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace llvm {
class Function;
class Type;
} // namespace llvm

namespace yume {
namespace ast {
class Expr;
class Program;
class StructDecl;
class Stmt;
} // namespace ast
namespace ty {
class Type;
}

struct InScope {
  Val value;
  const ast::AST& ast;
  bool owning{};
};

/// The `Compiler` the the primary top-level type during compilation. A single instance is created during the
/// compilation process.
class Compiler : public CRTPWalker<Compiler> {
  vector<SourceFile> m_sources;
  TypeHolder m_types;
  vector<Fn> m_fns{};
  vector<Struct> m_structs{};
  vector<Ctor> m_ctors{};
  std::queue<Fn*> m_decl_queue{};
  unique_ptr<semantic::TypeWalker> m_walker;

  Fn* m_current_fn{};
  std::map<string, InScope> m_scope{};

  unique_ptr<llvm::LLVMContext> m_context;
  unique_ptr<llvm::IRBuilder<>> m_builder;
  unique_ptr<llvm::Module> m_module;
  unique_ptr<llvm::TargetMachine> m_targetMachine;

  friend semantic::TypeWalker;
  friend CRTPWalker;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }

  explicit Compiler(vector<SourceFile> source_files);
  /// Begin compilation!
  void run();

  /// Declare a function in bytecode, or get an existing declaration.
  auto declare(Fn&, bool mangle = true) -> llvm::Function*;
  auto declare(Ctor&) -> llvm::Function*;

  static auto create_struct(ast::StructDecl&, const optional<string>& name_override = {}) -> unique_ptr<ty::Struct>;

  /// Compile the body of a function.
  void define(Fn&);

  void body_statement(const ast::Stmt&);
  auto decl_statement(ast::Stmt&, ty::Type* parent = nullptr, ast::Program* member = nullptr) -> DeclLike;
  auto body_expression(const ast::Expr& expr, bool mut = false) -> Val;

  void write_object(const char* filename, bool binary);

  /// Convert a type into its corresponding llvm type
  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  /// Default-constructs an object of specified type
  auto default_init(const ty::Type& type) -> Val;
  void destruct(Val val, const ty::Type& type);

  auto mangle_name(Fn& fn) -> string;
  auto mangle_name(Ctor& ctor) -> string;
  auto mangle_name(const ty::Type& ast_type, DeclLike parent) -> string;

  [[nodiscard]] auto source_files() -> const auto& { return m_sources; }

private:
  template <typename T> void statement(const T& stat) {
    throw std::runtime_error("Unknown statement "s + stat.kind_name());
  }

  template <typename T> auto expression(const T& expr, [[maybe_unused]] bool mut = false) -> Val {
    throw std::runtime_error("Unknown expression "s + expr.kind_name());
  }

  void destruct_all_in_scope(ast::FnDecl& scope_parent);

  auto known_type(const string& str) -> ty::Type&;

  auto primitive(Fn* fn, const vector<llvm::Value*>& args, const vector<const ty::Type*>& types, const ty::Type* ret_ty)
      -> optional<Val>;
  auto int_bin_primitive(const string& primitive, const vector<llvm::Value*>& args) -> Val;

  void walk_types(DeclLike);
};
} // namespace yume
