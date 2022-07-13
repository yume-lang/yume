#pragma once

#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "token.hpp"
#include "util.hpp"
#include <compare>
#include <iosfwd>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {
class Function;
class Value;
class BasicBlock;
} // namespace llvm

namespace yume {
class Compiler;

namespace ty {
class Generic;
class Type;
} // namespace ty

/// A mapping between type variables and substitutions for them.
struct Instantiation {
  std::map<const ty::Generic*, const ty::Type*> sub{};

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

using substitution_t = std::map<string, const ty::Type*>;

/// A function declaration in the compiler.
/**
 * The primary use of this struct is to bind together the AST declaration of a function (`ast::FnDecl`) and the bytecode
 * body of a function (`llvm::Function`).
 * A function template may also be a `Fn`. Since a template doesn't actually have a body, its `m_llvm_fn` is `nullptr`.
 * All the instantiations of a template are stored in `m_instantiations`.
 */
struct Fn {
  using decl_t = ast::FnDecl;
  using call_t = ast::CallExpr;

  ast::FnDecl& ast;
  /// If this function is in the body of a struct, this points to its type. Used for the `self` type.
  ty::Type* self_t{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  vector<unique_ptr<ty::Generic>> type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  substitution_t subs{};
  std::map<Instantiation, unique_ptr<Fn>> instantiations{};
  llvm::Function* llvm{};
  /// A basic block where are allocations for local variables should go. It is placed \e before the "entrypoint".
  llvm::BasicBlock* decl_bb{};

  Fn(ast::FnDecl& ast_decl, ty::Type* parent = nullptr, ast::Program* member = nullptr, substitution_t subs = {},
     vector<unique_ptr<ty::Generic>> type_args = {})
      : ast(ast_decl), self_t(parent), member(member), type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto body() const -> const auto& { return ast.body(); }

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] static auto overload_name(const call_t& ast) -> string;
  [[nodiscard]] static auto arg_type(const decl_t::arg_t& ast) -> const ty::Type*;

  [[nodiscard]] auto declaration(Compiler& compiler, bool mangle = true) -> llvm::Function*;

  [[nodiscard]] auto get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Fn&>;
  [[nodiscard]] auto create_instantiation(Instantiation& instantiate) -> Fn&;

  operator llvm::Function*() const { return llvm; }
};

struct Struct {
  using decl_t = ast::StructDecl;

  ast::StructDecl& ast;
  /// The type of this struct. Used for the `self` type.
  ty::Type* self_t{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  vector<unique_ptr<ty::Generic>> type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  substitution_t subs{};
  std::map<Instantiation, unique_ptr<Struct>> instantiations{};

  Struct(ast::StructDecl& ast_decl, ty::Type* type = nullptr, ast::Program* member = nullptr, substitution_t subs = {},
         vector<unique_ptr<ty::Generic>> type_args = {})
      : ast(ast_decl), self_t(type), member(member), type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto body() const -> const auto& { return ast.body(); }

  [[nodiscard]] auto name() const -> string;

  [[nodiscard]] auto get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Struct&>;
  [[nodiscard]] auto create_instantiation(Instantiation& instantiate) -> Struct&;
};

struct Ctor {
  using decl_t = ast::CtorDecl;
  using call_t = ast::CtorExpr;

  ast::CtorDecl& ast;
  ty::Type* self_t{};
  ast::Program* member{};
  llvm::Function* llvm{};

  Ctor(ast::CtorDecl& ast_decl, ty::Type* type = nullptr, ast::Program* member = nullptr)
      : ast(ast_decl), self_t(type), member(member) {}

  [[nodiscard]] auto declaration(Compiler& compiler) -> llvm::Function*;

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] static auto overload_name(const call_t& ast) -> string;
  [[nodiscard]] static auto arg_type(const decl_t::arg_t& ast) -> const ty::Type*;
};

template <typename R, typename... Ts> struct DeclLikeVisitor : Ts... {
  using Ts::operator()...;
  auto operator()(std::monostate /* ignored */) -> R { return R(); };
};
template <typename R, typename... Ts> DeclLikeVisitor(R, Ts...) -> DeclLikeVisitor<R, Ts...>;

using DeclLike_t = std::variant<std::monostate, Fn*, Struct*, Ctor*>;

struct DeclLike : public DeclLike_t {
  using DeclLike_t::DeclLike_t;

  template <typename R = void, typename... Ts> auto visit_decl(Ts... ts) {
    return std::visit(DeclLikeVisitor<R, Ts...>{ts...}, *this);
  }
  template <typename R = void, typename... Ts> [[nodiscard]] auto visit_decl(Ts... ts) const {
    return std::visit(DeclLikeVisitor<R, Ts...>{ts...}, *this);
  }

  [[nodiscard]] auto subs() const -> const substitution_t* {
    return visit_decl<substitution_t*>([](Ctor* /*decl*/) -> substitution_t* { return nullptr; },
                                       [](auto* decl) -> substitution_t* {
                                         if (decl == nullptr)
                                           return nullptr;
                                         return &decl->subs;
                                       });
  };

  [[nodiscard]] auto ast() const -> const ast::AST* {
    return visit_decl<ast::AST*>([](auto* decl) -> ast::AST* { return &decl->ast; });
  };

  [[nodiscard]] auto self_t() const -> const ty::Type* {
    return visit_decl<ty::Type*>([](const auto& decl) { return decl->self_t; });
  };
};

template <typename R = void, typename... Ts> [[deprecated]] auto visit_decl(DeclLike decl_like, Ts... ts) {
  return std::visit(DeclLikeVisitor<R, Ts...>{ts...}, decl_like);
}

/// A value of a complied expression.
/**
 * Note that this struct is mostly useless, it is a very thin wrapper around `llvm::Value`. It may be removed in the
 * future.
 */
struct Val {
  llvm::Value* llvm{};

  /* implicit */ Val(llvm::Value* llvm_val) : llvm(llvm_val) {}

  /* implicit */ operator llvm::Value*() const { return llvm; }
};

/// A source file with its associated Syntax Tree.
struct SourceFile {
  const string name;
  vector<yume::Token> tokens;
  ast::TokenIterator iterator;
  unique_ptr<ast::Program> program;

  SourceFile(std::istream& in, string name)
      : name(move(name)), tokens(yume::tokenize(in, this->name)), iterator{tokens.begin(), tokens.end()} {
#ifdef YUME_SPEW_LIST_TOKENS
    llvm::outs() << "tokens:\n";
    for (auto& i : tokens) {
      llvm::outs() << "  " << i << "\n";
    }
    llvm::outs() << "\n";
    llvm::outs().flush();
#endif
    program = ast::Program::parse(iterator);
  }
};
} // namespace yume
