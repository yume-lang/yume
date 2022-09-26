#pragma once

#include "ast/crtp_walker.hpp"
#include "compiler/scope_container.hpp"
#include "semantic/type_walker.hpp"
#include "type_holder.hpp"
#include "util.hpp"
#include "vals.hpp"
#include <deque>
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
class Value;
} // namespace llvm

namespace yume {
namespace ast {
class AST;
class Expr;
struct Program;
class StructDecl;
class Stmt;
} // namespace ast
namespace ty {
class Struct;
class BaseType;
} // namespace ty

/// The `Compiler` the the primary top-level type during compilation. A single instance is created during the
/// compilation process.
class Compiler : public CRTPWalker<Compiler> {
  vector<SourceFile> m_sources;
  TypeHolder m_types;
  std::deque<Fn> m_fns{};
  std::deque<Struct> m_structs{};
  std::deque<Fn> m_ctors{};
  std::deque<Const> m_consts{};
  std::queue<DeclLike> m_decl_queue{};
  unique_ptr<semantic::TypeWalker> m_walker;

  Fn* m_current_fn{};
  /// Local variables currently in function scope. Used for destructors
  ScopeContainer<InScope> m_scope{};
  /// In a constructor, the object being constructed, implicitly created by the compiler.
  optional<InScope> m_scope_ctor{};

  Struct* m_slice_struct{};

  unique_ptr<llvm::LLVMContext> m_context;
  unique_ptr<llvm::IRBuilder<>> m_builder;
  unique_ptr<llvm::Module> m_module;
  unique_ptr<llvm::TargetMachine> m_targetMachine;

  friend semantic::TypeWalker;
  friend CRTPWalker;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }
  [[nodiscard]] auto builder() const -> const auto& { return m_builder; }

  Compiler(const optional<string>& target_triple, vector<SourceFile> source_files);
  /// Begin compilation!
  void run();

  /// Declare a function/constructor in bytecode, or get an existing declaration.
  auto declare(Fn&) -> llvm::Function*;

  auto create_struct(Struct&) -> bool;

  /// Compile the body of a function or constructor.
  void define(Fn&);
  void define(Const&);

  void body_statement(ast::Stmt&);
  auto decl_statement(ast::Stmt&, optional<ty::Type> parent = std::nullopt, ast::Program* member = nullptr) -> DeclLike;
  auto body_expression(ast::Expr& expr) -> Val;

  void write_object(const char* filename, bool binary);

  /// Convert a type into its corresponding LLVM type
  auto llvm_type(ty::Type type) -> llvm::Type*;

  auto ptr_bitsize() -> unsigned int;

  /// Default-constructs an object of specified type \p type .
  auto default_init(ty::Type type) -> Val;
  /// Destructs an object \p val of specified type \p type .
  void destruct(Val val, ty::Type type);

  [[nodiscard]] auto source_files() -> const auto& { return m_sources; }

private:
  template <typename T>
  requires (!std::is_const_v<T>)
  void statement(T& stat) {
    throw std::runtime_error("Unknown statement "s + stat.kind_name());
  }

  template <typename T>
  requires (!std::is_const_v<T>)
  auto expression(T& expr) -> Val {
    throw std::runtime_error("Unknown expression "s + expr.kind_name());
  }

  /// Create a temporary `IRBuilder` which points to the entrypoint of the current function being compiled. This is used
  /// for variable declarations, which are always at the entrypoint, as to not cause a stack overflow with locals.
  auto entrypoint_builder() -> llvm::IRBuilder<>;

  /// Common code between defining functions and constructors. \see define .
  void setup_fn_base(Fn&);

  /// Run the destructors for every owned local variable in the current scope. Should be run when exiting a scope.
  void destruct_last_scope();
  /// Run the destructors for every owned local variable in all scopes of the current function. Should be run when
  /// returning from a function in any way.
  void destruct_all_scopes();
  /// Create a new temporary variable in the current scope, that will automatically be destructed at the end of the
  /// scope.
  void make_temporary_in_scope(Val& val, const ast::AST& ast, const string& name = "tmp"s);

  void expose_parameter_as_local(ty::Type type, const string& name, const ast::AST& ast, Val val);

  auto create_malloc(llvm::Type* base_type, Val slice_size, string_view name = ""sv) -> Val;
  auto create_malloc(llvm::Type* base_type, uint64_t slice_size, string_view name = ""sv) -> Val;

  /// Handle all primitive, built-in functions
  auto primitive(Fn* fn, const vector<Val>& args, const vector<ty::Type>& types) -> optional<Val>;
  /// Handle primitive functions taking two integral values, such as most arithmetic operations (add, multiply, etc).
  auto int_bin_primitive(const string& primitive, const vector<Val>& args) -> Val;

  /// Instruct the `TypeWalker` to perform semantic analysis and infer types for the given declaration.
  void walk_types(DeclLike);

  void declare_default_ctor(Struct&);
};
} // namespace yume
