//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_COMPILER_HPP
#define YUME_CPP_COMPILER_HPP

#include "util.hpp"
#include "ast.hpp"
#include <map>
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

namespace yume {
using namespace llvm;

class Compiler {
  unique_ptr<ast::Program> m_program;
  std::map<string, llvm::Type*> m_known_types;

  unique_ptr<LLVMContext> m_context;
  unique_ptr<IRBuilder<>> m_builder;
  unique_ptr<Module> m_module;
  unique_ptr<TargetMachine> m_targetMachine;

public:
  [[nodiscard]] auto module() const -> const auto& { return m_module; }

  explicit Compiler(unique_ptr<ast::Program> program);

  void add(const ast::FnDeclStatement& fn_decl);

  void write_object(const char* filename, bool binary);

  auto convert_type(const ast::Type& ast_type) -> llvm::Type*;

  auto mangle_name(const ast::FnDeclStatement& fn_decl) -> string;

  auto mangle_name(const ast::Type& ast_type) -> string;
};
} // namespace yume

#endif // YUME_CPP_COMPILER_HPP
