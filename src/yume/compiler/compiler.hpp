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
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
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
using namespace llvm;

/// The `Compiler` the the primary top-level type during compilation. A single instance is created during the
/// compilation process.
class Compiler : public CRTPWalker<Compiler> {
  vector<SourceFile> m_sources;
  TypeHolder m_types;
  vector<Fn> m_fns{};
  vector<Struct> m_structs{};
  std::queue<Fn*> m_decl_queue{};
  unique_ptr<semantic::TypeWalker> m_walker;

  Fn* m_current_fn{};
  std::map<string, Val> m_scope{};

  unique_ptr<LLVMContext> m_context;
  unique_ptr<IRBuilder<>> m_builder;
  unique_ptr<Module> m_module;
  unique_ptr<TargetMachine> m_targetMachine;

  friend semantic::TypeWalker;
  friend CRTPWalker;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }

  explicit Compiler(vector<SourceFile> source_files);
  /// Begin compilation!
  void run();

  /// Declare a function in bytecode, or get an existing declaration.
  auto declare(Fn&, bool mangle = true) -> llvm::Function*;

  /// Compile the body of a function.
  void define(Fn&);

  void body_statement(const ast::Stmt&);
  void decl_statement(ast::Stmt&, ty::Type* parent = nullptr, ast::Program* member = nullptr);
  auto body_expression(const ast::Expr& expr, bool mut = false) -> Val;

  void write_object(const char* filename, bool binary);

  /// Convert a type into its corresponding llvm type
  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  /// Default-constructs an object of specified type
  auto default_init(const ty::Type& type) -> Val;

  auto mangle_name(const Fn& fn_decl) -> string;
  auto mangle_name(const ty::Type& ast_type, const Fn& parent) -> string;

  [[nodiscard]] auto source_files() -> const auto& { return m_sources; }

private:
  template <typename T> void statement(const T& stat) {
    throw std::runtime_error("Unknown statement "s + stat.kind_name());
  }

  template <typename T> auto expression(const T& expr, [[maybe_unused]] bool mut = false) -> Val {
    throw std::runtime_error("Unknown expression "s + expr.kind_name());
  }

  auto known_type(const string& str) -> ty::Type&;

  auto int_bin_primitive(const string& primitive, const vector<Val>& args) -> Val;

  void walk_types();
};
} // namespace yume
