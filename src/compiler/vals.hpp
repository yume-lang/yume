#pragma once

#include "../ast.hpp"
#include "../token.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include <compare>
#include <iosfwd>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class Function;
class Value;
} // namespace llvm

namespace yume {
class Compiler;

/// A mapping between type variables and substitutions for them.
struct Instantiation {
  std::map<const ty::Generic*, const ty::Type*> m_sub{};

#if __cpp_lib_three_way_comparison >= 201907L
  auto operator<=>(const Instantiation& other) const = default;
#else
  auto operator<=>(const Instantiation& other) const -> std::weak_ordering {
    if (m_sub == other.m_sub)
      return std::weak_ordering::equivalent;
    if (m_sub < other.m_sub)
      return std::weak_ordering::less;
    return std::weak_ordering::greater;
  }
#endif
};

/// A function declaration in the compiler.
/**
 * The primary use of this struct is to bind together the AST declaration of a function (`ast::FnDecl`) and the bytecode
 * body of a function (`llvm::Function`).
 * A function template may also be a `Fn`. Since a template doesn't actually have a body, its `m_llvm_fn` is `nullptr`.
 * All the instantiations of a template are stored in `m_instantiations`.
 */
struct Fn {
  ast::FnDecl& m_ast_decl;
  /// If this function is in the body of a struct, this points to its type. Used for the `self` type.
  ty::Type* m_parent{};
  /// The program this declaration is a member of.
  ast::Program* m_member{};
  vector<unique_ptr<ty::Generic>> m_type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  std::map<string, const ty::Type*> m_subs{};
  std::map<Instantiation, unique_ptr<Fn>> m_instantiations{};
  llvm::Function* m_llvm_fn{};
  /// A basic block where are allocations for local variables should go. It is placed \e before the "entrypoint".
  llvm::BasicBlock* m_decl_bb{};

  Fn(ast::FnDecl& ast_decl, ty::Type* parent = nullptr, ast::Program* member = nullptr,
     std::map<string, const ty::Type*> subs = {}, vector<unique_ptr<ty::Generic>> type_args = {})
      : m_ast_decl(ast_decl), m_parent(parent), m_member(member), m_type_args(move(type_args)), m_subs(move(subs)) {}

  [[nodiscard]] auto body() const -> const auto& { return m_ast_decl.body(); }

  [[nodiscard]] auto name() const { return m_ast_decl.name(); }

  [[nodiscard]] auto ast() const -> auto& { return m_ast_decl; }
  [[nodiscard]] auto llvm() const -> llvm::Function* { return m_llvm_fn; }
  [[nodiscard]] auto parent() const -> ty::Type* { return m_parent; }

  [[nodiscard]] auto declaration(Compiler& compiler, bool mangle = true) -> llvm::Function*;

  operator llvm::Function*() const { return m_llvm_fn; }
};

/// A value of a complied expression.
/**
 * Note that this struct is mostly useless, it is a very thin wrapper around `llvm::Value`. It may be removed in the
 * future.
 */
struct Val {
  llvm::Value* m_llvm_val{};

  /* implicit */ Val(llvm::Value* llvm_val) : m_llvm_val(llvm_val) {}

  [[nodiscard]] auto llvm() const -> llvm::Value* { return m_llvm_val; }

  operator llvm::Value*() const { return m_llvm_val; }
};

/// A source file with its associated Syntax Tree.
struct SourceFile {
  const string m_name;
  vector<yume::Token> m_tokens;
  ast::TokenIterator m_iterator;
  unique_ptr<ast::Program> m_program;

  SourceFile(std::istream& in, string name)
      : m_name(std::move(name)), m_tokens(yume::tokenize(in, m_name)), m_iterator{m_tokens.begin(), m_tokens.end()} {
#ifdef YUME_SPEW_LIST_TOKENS
    llvm::outs() << "tokens:\n";
    for (auto& i : m_tokens) {
      llvm::outs() << "  " << i << "\n";
    }
    llvm::outs() << "\n";
    llvm::outs().flush();
#endif
    m_program = ast::Program::parse(m_iterator);
  }
};
} // namespace yume
