#pragma once

#include "../ast.hpp"
#include "../token.hpp"
#include "../util.hpp"
#include <iosfwd>
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
namespace ty {
class Type;
}

struct Fn {
  ast::FnDecl& m_ast_decl;
  ty::Type* m_parent{};
  // TODO: multiple instantiations
  llvm::Function* m_llvm_fn{};

  std::map<const ast::CallExpr*, Fn*> selected_overload{};

  [[nodiscard]] auto inline body() const -> const auto& { return m_ast_decl.body(); }

  [[nodiscard]] auto inline name() const { return m_ast_decl.name(); }

  [[nodiscard]] auto inline ast() const -> auto& { return m_ast_decl; }

  [[nodiscard]] auto inline llvm() const -> llvm::Function* { return m_llvm_fn; }

  [[nodiscard]] auto inline parent() const -> ty::Type* { return m_parent; }

  [[nodiscard]] auto declaration(Compiler& compiler, bool mangle = true) -> llvm::Function*;

  [[nodiscard]] auto inline selected_overload_for(const ast::CallExpr& call) -> Fn& { return *selected_overload.at(&call); }

  operator llvm::Function*() const { // NOLINT(google-explicit-constructor)
    return m_llvm_fn;
  }
};

struct Val {
  llvm::Value* m_llvm_val{};
  ty::Type* m_type{};

  /* implicit */ inline Val(llvm::Value* llvm_val) : m_llvm_val(llvm_val) {}
  inline Val(llvm::Value* llvm_val, ty::Type* type) : m_llvm_val(llvm_val), m_type(type) {}
  Val(const Val&) noexcept = default;
  Val(Val&&) noexcept = default;
  auto operator=(const Val&) noexcept -> Val& = default;
  auto operator=(Val&&) noexcept -> Val& = default;
  virtual ~Val() = default;

  [[nodiscard]] auto inline llvm() const -> llvm::Value* { return m_llvm_val; }
  [[deprecated]] [[nodiscard]] auto inline type() const -> ty::Type* { return m_type; }

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
} // namespace yume
