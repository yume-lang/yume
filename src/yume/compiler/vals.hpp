#pragma once

#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "token.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <compare>
#include <iosfwd>
#include <llvm/Support/Casting.h>
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
} // namespace llvm

namespace yume {
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

struct FnBase {
  ast::AST& ast;
  /// If this function is in the body of a struct, this points to its type. Used for the `self` type.
  ty::Type* self_ty{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  llvm::Function* llvm{};

  operator llvm::Function*() const { return llvm; }
};

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

  FnBase base;
  vector<unique_ptr<ty::Generic>> type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  substitution_t subs{};
  std::map<Instantiation, unique_ptr<Fn>> instantiations{};

  Fn(ast::FnDecl& ast_decl, ty::Type* parent = nullptr, ast::Program* member = nullptr, substitution_t subs = {},
     vector<unique_ptr<ty::Generic>> type_args = {})
      : base{ast_decl, parent, member}, type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto ast() const -> const auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto ast() -> auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto body() const -> const auto& { return ast().body(); }
  [[nodiscard]] auto get_self_ty() const -> ty::Type* { return base.self_ty; };

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] static auto overload_name(const call_t& ast) -> string;
  [[nodiscard]] static auto arg_type(const decl_t::arg_t& ast) -> const ty::Type*;
  [[nodiscard]] static auto arg_name(const decl_t::arg_t& ast) -> string;
  [[nodiscard]] static auto common_ast(const decl_t::arg_t& ast) -> const ast::AST&;

  [[nodiscard]] auto get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Fn&>;
  [[nodiscard]] auto create_instantiation(Instantiation& instantiate) -> Fn&;
};

struct Struct {
  using decl_t = ast::StructDecl;

  ast::StructDecl& st_ast;
  /// The type of this struct. Used for the `self` type.
  ty::Type* self_ty{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  vector<unique_ptr<ty::Generic>> type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  substitution_t subs{};
  std::map<Instantiation, unique_ptr<Struct>> instantiations{};

  Struct(ast::StructDecl& ast_decl, ty::Type* type = nullptr, ast::Program* member = nullptr, substitution_t subs = {},
         vector<unique_ptr<ty::Generic>> type_args = {})
      : st_ast(ast_decl), self_ty(type), member(member), type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto ast() const -> const auto& { return st_ast; }
  [[nodiscard]] auto ast() -> auto& { return st_ast; }
  [[nodiscard]] auto body() const -> const auto& { return st_ast.body(); }
  [[nodiscard]] auto get_self_ty() const -> ty::Type* { return self_ty; };

  [[nodiscard]] auto name() const -> string;

  [[nodiscard]] auto get_or_create_instantiation(Instantiation& instantiate) -> std::pair<bool, Struct&>;
  [[nodiscard]] auto create_instantiation(Instantiation& instantiate) -> Struct&;
};

struct Ctor {
  using decl_t = ast::CtorDecl;
  using call_t = ast::CtorExpr;

  FnBase base;

  Ctor(ast::CtorDecl& ast_decl, ty::Type* type = nullptr, ast::Program* member = nullptr)
      : base{ast_decl, type, member} {}

  [[nodiscard]] auto ast() const -> const auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto ast() -> auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto get_self_ty() const -> ty::Type* { return base.self_ty; };

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] static auto overload_name(const call_t& ast) -> string;
  [[nodiscard]] static auto arg_type(const decl_t::arg_t& ast) -> const ty::Type*;
  [[nodiscard]] static auto arg_name(const decl_t::arg_t& ast) -> string;
  [[nodiscard]] static auto common_ast(const decl_t::arg_t& ast) -> const ast::AST&;
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
    return visit_decl<const ast::AST*>([](const auto* decl) -> const ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto self_ty() const -> const ty::Type* {
    return visit_decl<ty::Type*>([](const auto* decl) { return decl->get_self_ty(); });
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
