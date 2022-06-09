//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_COMPILER_HPP
#define YUME_CPP_COMPILER_HPP

#include "../ast.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include "crtp_walker.hpp"
#include "type_holder.hpp"
#include "type_walker.hpp"
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

class Compiler : public CRTPWalker<Compiler> {
  std::vector<SourceFile> m_sources;
  TypeHolder m_types;
  vector<Fn> m_fns{};
  std::queue<Fn*> m_decl_queue{};
  unique_ptr<TypeWalker> m_walker;

  Fn* m_current_fn{};
  std::map<string, Val> m_scope{};

  unique_ptr<LLVMContext> m_context;
  unique_ptr<IRBuilder<>> m_builder;
  unique_ptr<Module> m_module;
  unique_ptr<TargetMachine> m_targetMachine;

  friend TypeWalker;
  friend CRTPWalker;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }

  explicit Compiler(std::vector<SourceFile> source_files);
  void run();

  [[nodiscard]] auto declare(Fn&, bool mangle = true) -> llvm::Function*;

  void define(Fn&);

  void body_statement(const ast::Stmt&);
  void decl_statement(ast::Stmt&, ty::Type* parent = nullptr, ast::Program* member = nullptr);
  auto body_expression(const ast::Expr& expr, bool mut = false) -> Val;

  void write_object(const char* filename, bool binary);

  auto convert_type(const ast::Type& ast_type, const ty::Type* parent = nullptr, Fn* context = nullptr) -> const ty::Type&;

  auto llvm_type(const ty::Type& type) -> llvm::Type*;

  auto mangle_name(const Fn& fn_decl) -> string;

  auto mangle_name(const ast::Type& ast_type, const Fn& parent) -> string;

  [[nodiscard]] inline auto source_files() -> const auto& { return m_sources; }

private:
  template <typename T> inline void statement(const T& stat) {
    throw std::runtime_error("Unknown statement "s + stat.kind_name());
  }

  template <typename T> inline auto expression(const T& expr, [[maybe_unused]] bool mut = false) -> Val {
    throw std::runtime_error("Unknown expression "s + expr.kind_name());
  }

  auto known_type(const string& str) -> ty::Type&;

  auto int_bin_primitive(const string& primitive, const vector<Val>& args) -> Val;

  void walk_types();
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
