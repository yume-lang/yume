#pragma once

#include "../ast.hpp"
#include "../token.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include <iosfwd>
#include <llvm/ADT/DenseMap.h>
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

struct Instantiation {
  std::map<const ty::Generic*, const ty::Type*> m_sub{};

  auto operator<=>(const Instantiation& other) const = default;
};

struct Fn {
  ast::FnDecl& m_ast_decl;
  ty::Type* m_parent{};
  ast::Program* m_member{};
  vector<unique_ptr<ty::Generic>> m_type_args{};
  std::map<string, const ty::Type*> m_subs{};
  std::map<Instantiation, unique_ptr<Fn>> m_instantiations{};
  llvm::Function* m_llvm_fn{};

  Fn(ast::FnDecl& ast_decl, ty::Type* parent = nullptr, ast::Program* member = nullptr,
     std::map<string, const ty::Type*> subs = {}, vector<unique_ptr<ty::Generic>> type_args = {})
      : m_ast_decl(ast_decl), m_parent(parent), m_member(member), m_type_args(move(type_args)), m_subs(move(subs)) {}

  [[nodiscard]] auto inline body() const -> const auto& { return m_ast_decl.body(); }

  [[nodiscard]] auto inline name() const { return m_ast_decl.name(); }

  [[nodiscard]] auto inline ast() const -> auto& { return m_ast_decl; }

  [[nodiscard]] auto inline llvm() const -> llvm::Function* { return m_llvm_fn; }

  [[nodiscard]] auto inline parent() const -> ty::Type* { return m_parent; }

  [[nodiscard]] auto declaration(Compiler& compiler, bool mangle = true) -> llvm::Function*;

  operator llvm::Function*() const { return m_llvm_fn; }
};

struct Val {
  llvm::Value* m_llvm_val{};

  /* implicit */ inline Val(llvm::Value* llvm_val) : m_llvm_val(llvm_val) {}

  [[nodiscard]] auto inline llvm() const -> llvm::Value* { return m_llvm_val; }

  operator llvm::Value*() const { return m_llvm_val; }
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
} // namespace yume
