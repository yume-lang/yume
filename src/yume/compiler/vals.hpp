#pragma once

#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "token.hpp"
#include "ty/substitution.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <compare>
#include <functional>
#include <iosfwd>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {
class Function;
class Value;
} // namespace llvm

namespace yume {

struct FnArg {
  ty::Type type;
  string name;
  const ast::AST& ast;

  FnArg(ty::Type type, string name, const ast::AST& ast) : type{type}, name{move(name)}, ast{ast} {};
};

/// A function declaration in the compiler.
/**
 * The primary use of this structure is to bind together the AST declaration of a function (`ast::FnDecl`) and the
 * bytecode body of a function (`llvm::Function`).
 * A function template may also be a `Fn`. Since a template doesn't actually have a body, its `m_llvm_fn` is `nullptr`.
 * All the instantiations of a template are stored in `instantiations`.
 */
struct Fn {
  using decl_t = ast::FnDecl;
  using call_t = ast::CallExpr;

  /// The ast node that defines this declaration.
  ast::Decl& ast_decl;
  /// If this function is in the body of a struct, this points to its type. Used for the `self` type.
  optional<ty::Type> self_ty{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  Substitution subs{};
  /// The LLVM function definition corresponding to this function or constructor.
  llvm::Function* llvm{};

  vector<unique_ptr<ty::Generic>> type_args{};
  std::map<Substitution, unique_ptr<Fn>> instantiations{};

  Fn(ast::Decl& ast_decl, optional<ty::Type> parent = std::nullopt, ast::Program* member = nullptr,
     Substitution subs = {}, vector<unique_ptr<ty::Generic>> type_args = {})
      : ast_decl{ast_decl}, self_ty{parent}, member{member}, subs{move(subs)}, type_args(move(type_args)) {}

  [[nodiscard]] auto fn_body() const -> const ast::FnDecl::body_t&;
  [[nodiscard]] auto compound_body() const -> const ast::Compound&;
  [[nodiscard]] auto ast() const -> const auto& { return ast_decl; }
  [[nodiscard]] auto ast() -> auto& { return ast_decl; }
  [[nodiscard]] auto get_self_ty() const -> optional<ty::Type> { return self_ty; }
  [[nodiscard]] auto get_subs() const -> const Substitution& { return subs; }
  [[nodiscard]] auto get_subs() -> Substitution& { return subs; }

  [[nodiscard]] auto ret() const -> optional<ty::Type>;
  [[nodiscard]] auto arg_count() const -> size_t;
  [[nodiscard]] auto arg_types() const -> vector<ty::Type>;
  [[nodiscard]] auto arg_names() const -> vector<string>;
  [[nodiscard]] auto args() const -> vector<FnArg>;
  [[nodiscard]] auto varargs() const -> bool;
  [[nodiscard]] auto primitive() const -> bool;
  [[nodiscard]] auto extern_decl() const -> bool;
  [[nodiscard]] auto extern_linkage() const -> bool;
  void make_extern_linkage(bool value = true);

  [[nodiscard]] auto name() const noexcept -> string;

  [[nodiscard]] auto get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Fn&>;
  [[nodiscard]] auto create_instantiation(Substitution& subs) noexcept -> Fn&;

private:
  template <typename... Ts>
  auto visit_decl(std::invocable<ast::FnDecl&, Ts...> auto fn_c, std::invocable<ast::CtorDecl&, Ts...> auto ct_c,
                  Ts... ts) const -> decltype(auto) {
    if (auto* fn_decl = dyn_cast<ast::FnDecl>(&ast_decl)) {
      return fn_c(*fn_decl, std::forward<Ts...>(ts)...);
    }
    return ct_c(cast<ast::CtorDecl>(ast_decl), std::forward<Ts...>(ts)...);
  }

  template <typename T, typename..., typename Vec = std::vector<T>>
  auto visit_map_args(T fn_c(ast::FnDecl::arg_t&), T ct_c(ast::CtorDecl::arg_t&)) const -> Vec {
    Vec vec = {};
    vec.reserve(arg_count());
    if (auto* fn_decl = dyn_cast<ast::FnDecl>(&ast_decl)) {
      for (auto& i : fn_decl->args())
        vec.emplace_back(std::move<T>(fn_c(i)));
    } else {
      for (auto& i : cast<ast::CtorDecl>(ast_decl).args())
        vec.emplace_back(std::move<T>(ct_c(i)));
    }
    return vec;
  }
};

/// A struct declaration in the compiler.
/**
 * Very similar to `Fn`, the primary use of this structure is to bind together the AST declaration of a struct
 * (`ast::StructDecl`) and the type this struct defines. A struct template may also be a `Struct`. All the
 * instantiations of a template are stored in `instantiations`.
 */
struct Struct {
  using decl_t = ast::StructDecl;

  ast::StructDecl& st_ast;
  /// The type of this struct. Used for the `self` type.
  optional<ty::Type> self_ty{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  vector<unique_ptr<ty::Generic>> type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  Substitution subs{};
  std::map<Substitution, unique_ptr<Struct>> instantiations{};

  Struct(ast::StructDecl& ast_decl, optional<ty::Type> type = std::nullopt, ast::Program* member = nullptr,
         Substitution subs = {}, vector<unique_ptr<ty::Generic>> type_args = {}) noexcept
      : st_ast(ast_decl), self_ty(type), member(member), type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto ast() const noexcept -> const auto& { return st_ast; }
  [[nodiscard]] auto ast() noexcept -> auto& { return st_ast; }
  [[nodiscard]] auto body() const noexcept -> const auto& { return st_ast.body(); }
  [[nodiscard]] auto body() noexcept -> auto& { return st_ast.body(); }
  [[nodiscard]] auto get_self_ty() const noexcept -> optional<ty::Type> { return self_ty; };
  [[nodiscard]] auto get_subs() const noexcept -> const Substitution& { return subs; };
  [[nodiscard]] auto get_subs() noexcept -> Substitution& { return subs; };

  [[nodiscard]] auto name() const noexcept -> string;

  [[nodiscard]] auto get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Struct&>;
  [[nodiscard]] auto create_instantiation(Substitution& subs) noexcept -> Struct&;
};

using DeclLike_t = std::variant<std::monostate, Fn*, Struct*>;

/// A common base between declarations in the compiler: `Fn` and `Struct`. Its value may also be absent
/// (`std::monostate`).
struct DeclLike : public DeclLike_t {
private:
  template <typename R, typename... Ts> struct DeclLikeVisitor : Ts... {
    using Ts::operator()...;
    auto operator()(std::monostate /* ignored */) noexcept(noexcept(R())) -> R { return R(); };
  };
  template <typename R, typename... Ts> DeclLikeVisitor(R, Ts...) -> DeclLikeVisitor<R, Ts...>;

public:
  using DeclLike_t::DeclLike_t;

  template <typename R = void, typename... Ts> auto visit_decl(Ts... ts) {
    return std::visit(DeclLikeVisitor<R, Ts...>{ts...}, *this);
  }
  template <typename R = void, typename... Ts> [[nodiscard]] auto visit_decl(Ts... ts) const {
    return std::visit(DeclLikeVisitor<R, Ts...>{ts...}, *this);
  }

  [[nodiscard]] auto subs() const noexcept -> const Substitution* {
    return visit_decl<const Substitution*>([](auto* decl) noexcept -> const Substitution* {
      if (decl == nullptr)
        return nullptr;
      auto& subs = decl->get_subs();
      if (auto self = decl->get_self_ty(); subs.empty() && self.has_value())
        if (auto* self_st = self->template base_dyn_cast<ty::Struct>())
          return &self_st->subs();

      return &subs;
    });
  };

  [[nodiscard]] auto subs() noexcept -> Substitution* {
    return visit_decl<Substitution*>([](auto* decl) noexcept -> Substitution* {
      if (decl == nullptr)
        return nullptr;
      return &decl->get_subs();
    });
  };

  [[nodiscard]] auto ast() const noexcept -> const ast::AST* {
    return visit_decl<const ast::AST*>([](const auto* decl) noexcept -> const ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto ast() noexcept -> ast::AST* {
    return visit_decl<ast::AST*>([](auto* decl) noexcept -> ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto self_ty() const noexcept -> optional<ty::Type> {
    return visit_decl<optional<ty::Type>>([](const auto* decl) noexcept { return decl->get_self_ty(); });
  };
};

struct InScope;

/// A value of a complied expression.
struct Val {
  llvm::Value* llvm{};
  InScope* scope{};

  /* implicit */ Val(llvm::Value* llvm_val) noexcept : llvm(llvm_val) {}
  Val(llvm::Value* llvm_val, InScope* scope_val) noexcept : llvm(llvm_val), scope(scope_val) {}

  /* implicit */ operator llvm::Value*() const { return llvm; }
};

/// A local variable in function scope. Used to track destructing when the scope ends.
struct InScope {
  Val value;
  const ast::AST& ast;
  /// Whether or not the local scope "owns" the variable. Unowned variables are not destructed at the end of the scope.
  bool owning{};
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
