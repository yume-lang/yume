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
class Value;
} // namespace llvm

namespace yume {
namespace ast {
class AST;
class Expr;
class Program;
class StructDecl;
class Stmt;
} // namespace ast
namespace ty {
class Struct;
class Type;
} // namespace ty

/// A local variable in function scope. Used to track destructing when the scope ends.
struct InScope {
  Val value;
  const ast::AST& ast;
  /// Whether or not the local scope "owns" the variable. Unowned variables are not destructed at the end of the scope.
  bool owning{};
};

/// The `Compiler` the the primary top-level type during compilation. A single instance is created during the
/// compilation process.
class Compiler : public CRTPWalker<Compiler> {
  vector<SourceFile> m_sources;
  TypeHolder m_types;
  std::list<Fn> m_fns{};
  std::list<Struct> m_structs{};
  std::list<Ctor> m_ctors{};
  std::queue<DeclLike> m_decl_queue{};
  unique_ptr<semantic::TypeWalker> m_walker;

  FnBase* m_current_fn{};
  /// Local variables currently in function scope. Used for destructors
  std::map<string, InScope> m_scope{};
  /// In a constructor, the object being constructed, implicitly created by the compiler.
  optional<InScope> m_scope_ctor{};

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
  /// Declare a constructor in bytecode, or get an existing declaration.
  auto declare(Ctor&) -> llvm::Function*;

  static auto create_struct(ast::StructDecl&, const optional<string>& name_override = {}) -> unique_ptr<ty::Struct>;

  /// Compile the body of a function.
  void define(Fn&);
  /// Compile the body of a constructor.
  void define(Ctor&);

  void body_statement(const ast::Stmt&);
  auto decl_statement(ast::Stmt&, ty::Type* parent = nullptr, ast::Program* member = nullptr) -> DeclLike;
  auto body_expression(const ast::Expr& expr) -> Val;

  void write_object(const char* filename, bool binary);

  /// Convert a type into its corresponding llvm type
  auto llvm_type(const ty::Type* type) -> llvm::Type*;

  /// Default-constructs an object of specified type \p type .
  auto default_init(const ty::Type* type) -> Val;
  /// Destructs an object \p val of specified type \p type .
  void destruct(Val val, const ty::Type* type);

  auto mangle_name(Fn& fn) -> string;
  auto mangle_name(Ctor& ctor) -> string;
  auto mangle_name(const ty::Type* ast_type, DeclLike parent) -> string;

  [[nodiscard]] auto source_files() -> const auto& { return m_sources; }

private:
  template <typename T> void statement(const T& stat) {
    throw std::runtime_error("Unknown statement "s + stat.kind_name());
  }

  template <typename T> auto expression(const T& expr) -> Val {
    throw std::runtime_error("Unknown expression "s + expr.kind_name());
  }

  /// Create a temporary `IRBuilder` which points to the entrypoint of the current function being compiled. This is used
  /// for variable declarations, which are always at the entrypoint, as to not cause a stack overflow with locals.
  auto entrypoint_builder() -> llvm::IRBuilder<>;

  /// Common code between defining functions and constructors. \see define .
  template <typename T> void setup_fn_base(T&);

  /// Run the destructors for every owned local variable in the current scope. Should be run when returning from a
  /// function in any way.
  void destruct_all_in_scope();

  /// Handle all primitive, built-in functions
  auto primitive(Fn* fn, const vector<llvm::Value*>& args, const vector<const ty::Type*>& types, const ty::Type* ret_ty)
      -> optional<Val>;
  /// Handle primitive functions taking two integral values, such as most arithmetic operations (add, multiply, etc).
  auto int_bin_primitive(const string& primitive, const vector<llvm::Value*>& args) -> Val;

  /// Instruct the `TypeWalker` to perform semantic analysis and infer types for the given declaration.
  void walk_types(DeclLike);

  void declare_default_ctor(Struct&);
};
} // namespace yume
