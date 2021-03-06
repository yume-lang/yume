#pragma once

#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "token.hpp"
#include "ty/substitution.hpp"
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
/// Common values between function declarations (`Fn`) and constructors (`Ctor`), which behave very similarly.
struct FnBase {
  /// The ast node that defines this declaration, being `ast::FnDecl` for `Fn` and `ast::CtorDecl` for `Ctor`. Should
  /// not be accessed directly, instead \see ast() on `Fn` and `Ctor`.
  ast::AST& ast;
  /// If this function is in the body of a struct, this points to its type. Used for the `self` type.
  optional<ty::Type> self_ty{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  Substitution subs{};
  /// The LLVM function definition corresponding to this function or constructor.
  llvm::Function* llvm{};

  operator llvm::Function*() const { return llvm; }
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

  FnBase base;
  vector<unique_ptr<ty::Generic>> type_args{};
  std::map<Substitution, unique_ptr<Fn>> instantiations{};

  Fn(ast::FnDecl& ast_decl, optional<ty::Type> parent = std::nullopt, ast::Program* member = nullptr,
     Substitution subs = {}, vector<unique_ptr<ty::Generic>> type_args = {})
      : base{ast_decl, parent, member, move(subs)}, type_args(move(type_args)) {}

  [[nodiscard]] auto ast() const -> const auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto ast() -> auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto body() const -> const auto& { return ast().body(); }
  [[nodiscard]] auto get_self_ty() const -> optional<ty::Type> { return base.self_ty; };
  [[nodiscard]] auto get_subs() const -> const Substitution& { return base.subs; };
  [[nodiscard]] auto get_subs() -> Substitution& { return base.subs; };

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] static auto overload_name(const call_t& ast) -> string;
  [[nodiscard]] static auto arg_type(const decl_t::arg_t& ast) -> optional<ty::Type>;
  [[nodiscard]] static auto arg_name(const decl_t::arg_t& ast) -> string;
  [[nodiscard]] static auto common_ast(const decl_t::arg_t& ast) -> const ast::AST&;

  [[nodiscard]] auto get_or_create_instantiation(Substitution& subs) -> std::pair<bool, Fn&>;
  [[nodiscard]] auto create_instantiation(Substitution& subs) -> Fn&;
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
         Substitution subs = {}, vector<unique_ptr<ty::Generic>> type_args = {})
      : st_ast(ast_decl), self_ty(type), member(member), type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto ast() const -> const auto& { return st_ast; }
  [[nodiscard]] auto ast() -> auto& { return st_ast; }
  [[nodiscard]] auto body() const -> const auto& { return st_ast.body(); }
  [[nodiscard]] auto body() -> auto& { return st_ast.body(); }
  [[nodiscard]] auto get_self_ty() const -> optional<ty::Type> { return self_ty; };
  [[nodiscard]] auto get_subs() const -> const Substitution& { return subs; };
  [[nodiscard]] auto get_subs() -> Substitution& { return subs; };

  [[nodiscard]] auto name() const -> string;

  [[nodiscard]] auto get_or_create_instantiation(Substitution& subs) -> std::pair<bool, Struct&>;
  [[nodiscard]] auto create_instantiation(Substitution& subs) -> Struct&;
};

/// A constructor declaration in the compiler.
/**
 * Very similar to `Fn`, the primary use of this structure is to bind together the AST declaration of a struct
 * (`ast::CtorDecl`) and the bytecode body of a function (`llvm::Function`).
 */
struct Ctor {
  using decl_t = ast::CtorDecl;
  using call_t = ast::CtorExpr;

  FnBase base;

  Ctor(ast::CtorDecl& ast_decl, optional<ty::Type> type = std::nullopt, ast::Program* member = nullptr)
      : base{ast_decl, type, member} {}

  [[nodiscard]] auto ast() const -> const auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto ast() -> auto& { return cast<decl_t>(base.ast); }
  [[nodiscard]] auto get_self_ty() const -> optional<ty::Type> { return base.self_ty; };
  [[nodiscard]] auto get_subs() const -> const Substitution& { return base.subs; };
  [[nodiscard]] auto get_subs() -> Substitution& { return base.subs; };

  [[nodiscard]] auto name() const -> string;
  [[nodiscard]] static auto overload_name(const call_t& ast) -> string;
  [[nodiscard]] static auto arg_type(const decl_t::arg_t& ast) -> optional<ty::Type>;
  [[nodiscard]] static auto arg_name(const decl_t::arg_t& ast) -> string;
  [[nodiscard]] static auto common_ast(const decl_t::arg_t& ast) -> const ast::AST&;
};

using DeclLike_t = std::variant<std::monostate, Fn*, Struct*, Ctor*>;

/// A common base between declarations in the compiler: `Fn`, `Struct`, and `Ctor`. Its value may also be absent
/// (`std::monostate`).
struct DeclLike : public DeclLike_t {
private:
  template <typename R, typename... Ts> struct DeclLikeVisitor : Ts... {
    using Ts::operator()...;
    auto operator()(std::monostate /* ignored */) -> R { return R(); };
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

  [[nodiscard]] auto subs() const -> const Substitution* {
    return visit_decl<Substitution*>([](auto* decl) -> Substitution* {
      if (decl == nullptr)
        return nullptr;
      return &decl->get_subs();
    });
  };

  [[nodiscard]] auto subs() -> Substitution* {
    return visit_decl<Substitution*>([](auto* decl) -> Substitution* {
      if (decl == nullptr)
        return nullptr;
      return &decl->get_subs();
    });
  };

  [[nodiscard]] auto ast() const -> const ast::AST* {
    return visit_decl<const ast::AST*>([](const auto* decl) -> const ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto ast() -> ast::AST* {
    return visit_decl<ast::AST*>([](auto* decl) -> ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto self_ty() const -> optional<ty::Type> {
    return visit_decl<optional<ty::Type>>([](const auto* decl) { return decl->get_self_ty(); });
  };
};

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
