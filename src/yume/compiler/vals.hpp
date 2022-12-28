#pragma once

#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "token.hpp"
#include "ty/substitution.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <algorithm>
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
class GlobalVariable;
} // namespace llvm

namespace yume {
namespace ty {
class Function;
}

struct FnArg {
  ty::Type type;
  string name;
  const ast::AST& ast;

  FnArg(ty::Type type, string name, const ast::AST& ast) : type{type}, name{move(name)}, ast{ast} {};
};

using Def_t = visitable_variant<ast::FnDecl*, ast::CtorDecl*, ast::LambdaExpr*>;
struct Def : public Def_t {
  using Def_t::Def_t;
};

/// A function declaration in the compiler.
/**
 * The primary use of this structure is to bind together the AST declaration of a function (`ast::FnDecl`) and the
 * bytecode body of a function (`llvm::Function`).
 * A function template may also be a `Fn`. Since a template doesn't actually have a body, its `m_llvm_fn` is `nullptr`.
 * All the instantiations of a template are stored in `instantiations`.
 */
struct Fn {
  /// The ast node that defines this declaration.
  Def def;
  const ty::Function* fn_ty{};
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

  Fn(Def def, ast::Program* member, optional<ty::Type> parent = std::nullopt, Substitution subs = {},
     vector<unique_ptr<ty::Generic>> type_args = {})
      : def{def}, self_ty{parent}, member{member}, subs{move(subs)}, type_args(move(type_args)) {}

  [[nodiscard]] auto fn_body() -> ast::FnDecl::Body&;
  [[nodiscard]] auto compound_body() -> ast::Compound&;
  [[nodiscard]] auto ast() const -> const ast::Stmt&;
  [[nodiscard]] auto ast() -> ast::Stmt&;
  [[nodiscard]] auto get_self_ty() const -> optional<ty::Type> { return self_ty; }
  [[nodiscard]] auto get_subs() const -> const Substitution& { return subs; }
  [[nodiscard]] auto get_subs() -> Substitution& { return subs; }

  [[nodiscard]] auto ret() const -> optional<ty::Type>;
  [[nodiscard]] auto arg_count() const -> size_t;
  [[nodiscard]] auto arg_types() const -> vector<ty::Type>;
  [[nodiscard]] auto arg_names() const -> vector<string>;
  [[nodiscard]] auto arg_nodes() const -> const vector<ast::TypeName>&;
  [[nodiscard]] auto args() const -> vector<FnArg>;
  [[nodiscard]] auto varargs() const -> bool;
  [[nodiscard]] auto primitive() const -> bool;
  [[nodiscard]] auto abstract() const -> bool;
  [[nodiscard]] auto extern_decl() const -> bool;
  [[nodiscard]] auto local() const -> bool;
  [[nodiscard]] auto extern_linkage() const -> bool;
  void make_extern_linkage(bool value = true);
  [[nodiscard]] auto has_annotation(const string& name) const -> bool;

  [[nodiscard]] auto name() const noexcept -> string;

  [[nodiscard]] auto get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Fn&>;
  [[nodiscard]] auto create_instantiation(Substitution& subs) noexcept -> Fn&;

private:
  template <std::invocable<ast::TypeName&> F, typename..., typename T = std::invoke_result_t<F, ast::TypeName&>>
  auto visit_map_args(F fn) const -> std::vector<T> {
    std::vector<T> vec = {};
    vec.reserve(arg_count());
    def.visit([&](auto* ast) {
      for (auto& i : ast->args)
        vec.emplace_back(std::move<T>(fn(i)));
    });
    return vec;
  }
};

struct VTableEntry {
  string name;
  vector<ty::Type> args;
  optional<ty::Type> ret;

  [[nodiscard]] auto operator==(const VTableEntry& other) const noexcept -> bool {
    auto args = llvm::zip(this->args, other.args);
    auto opaque_eq = [](const auto& p) { return std::get<0>(p).opaque_equal(std::get<1>(p)); };
    return this->name == other.name && std::all_of(args.begin(), args.end(), opaque_eq) &&
           this->ret.has_value() == other.ret.has_value() && (!this->ret || this->ret->opaque_equal(*other.ret));
  };
};

/// A struct declaration in the compiler.
/**
 * Very similar to `Fn`, the primary use of this structure is to bind together the AST declaration of a struct
 * (`ast::StructDecl`) and the type this struct defines. A struct template may also be a `Struct`. All the
 * instantiations of a template are stored in `instantiations`.
 */
struct Struct {
  ast::StructDecl& st_ast;
  /// The type of this struct. Used for the `self` type.
  optional<ty::Type> self_ty{};
  /// The program this declaration is a member of.
  ast::Program* member{};
  vector<unique_ptr<ty::Generic>> type_args{};
  /// If this is an instantiation of a template, a mapping between type variables and their substitutions.
  Substitution subs{};
  std::map<Substitution, unique_ptr<Struct>> instantiations{};
  std::vector<VTableEntry> vtable_members{};
  nullable<llvm::GlobalVariable*> vtable_memo{};

  Struct(ast::StructDecl& ast_decl, ast::Program* member, optional<ty::Type> type = std::nullopt,
         Substitution subs = {}, vector<unique_ptr<ty::Generic>> type_args = {}) noexcept
      : st_ast(ast_decl), self_ty(type), member(member), type_args(move(type_args)), subs(move(subs)) {}

  [[nodiscard]] auto ast() const noexcept -> const auto& { return st_ast; }
  [[nodiscard]] auto ast() noexcept -> auto& { return st_ast; }
  [[nodiscard]] auto body() const noexcept -> const auto& { return st_ast.body; }
  [[nodiscard]] auto body() noexcept -> auto& { return st_ast.body; }
  [[nodiscard]] auto get_self_ty() const noexcept -> optional<ty::Type> { return self_ty; };
  [[nodiscard]] auto get_subs() const noexcept -> const Substitution& { return subs; };
  [[nodiscard]] auto get_subs() noexcept -> Substitution& { return subs; };

  [[nodiscard]] auto name() const noexcept -> string;

  [[nodiscard]] auto get_or_create_instantiation(Substitution& subs) noexcept -> std::pair<bool, Struct&>;
  [[nodiscard]] auto create_instantiation(Substitution& subs) noexcept -> Struct&;
};

/// A constant declaration in the compiler.
struct Const {
  ast::ConstDecl& cn_ast;
  /// If this function is in the body of a struct, this points to its type.
  optional<ty::Type> self_ty;
  /// The program this declaration is a member of.
  ast::Program* member;
  llvm::GlobalVariable* llvm{};

  Const(ast::ConstDecl& ast_decl, ast::Program* member = nullptr, optional<ty::Type> parent = std::nullopt) noexcept
      : cn_ast(ast_decl), self_ty(parent), member(member) {}

  [[nodiscard]] auto ast() const noexcept -> const auto& { return cn_ast; }
  [[nodiscard]] auto ast() noexcept -> auto& { return cn_ast; }
  [[nodiscard]] auto get_self_ty() const noexcept -> optional<ty::Type> { return self_ty; };

  [[nodiscard]] auto name() const noexcept -> string;
  [[nodiscard]] auto referred_to_by(const ast::ConstExpr& expr) const -> bool {
    if (name() != expr.name)
      return false;

    if (self_ty.has_value() != expr.parent.has_value())
      return false;

    if (self_ty.has_value() && expr.parent.has_value())
      return self_ty->name() == expr.parent.value();

    return true;
  }
};

using DeclLike_t = visitable_variant<std::monostate, Fn*, Struct*, Const*>;

/// A common base between declarations in the compiler: `Fn`, `Struct` and `Const`. Its value may also be absent
/// (`std::monostate`).
struct DeclLike : public DeclLike_t {
public:
  using DeclLike_t::DeclLike_t;

  [[nodiscard]] auto subs() const noexcept -> const Substitution* {
    return visit([](std::monostate /*absent*/) noexcept -> const Substitution* { return nullptr; },
                 [](Const* /*cn*/) noexcept -> const Substitution* { return nullptr; },
                 [](auto* decl) noexcept -> const Substitution* {
                   if (decl == nullptr)
                     return nullptr;
                   auto& subs = decl->get_subs();
                   if (auto self = decl->get_self_ty(); subs.empty() && self.has_value()) {
                     if (auto* self_st = self->template base_dyn_cast<ty::Struct>())
                       return &self_st->subs();
                   }

                   return &subs;
                 });
  };

  [[nodiscard]] auto subs() noexcept -> Substitution* {
    return visit([](std::monostate /*absent*/) noexcept -> Substitution* { return nullptr; },
                 [](Const* /*cn*/) noexcept -> Substitution* { return nullptr; },
                 [](auto* decl) noexcept -> Substitution* {
                   if (decl == nullptr)
                     return nullptr;
                   return &decl->get_subs();
                 });
  };

  [[nodiscard]] auto ast() const noexcept -> const ast::AST* {
    return visit([](std::monostate /*absent*/) noexcept -> const ast::AST* { return nullptr; },
                 [](const auto* decl) noexcept -> const ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto ast() noexcept -> ast::AST* {
    return visit([](std::monostate /*absent*/) noexcept -> ast::AST* { return nullptr; },
                 [](auto* decl) noexcept -> ast::AST* { return &decl->ast(); });
  };

  [[nodiscard]] auto self_ty() const noexcept -> optional<ty::Type> {
    return visit([](std::monostate /*absent*/) noexcept { return optional<ty::Type>{}; },
                 [](const auto* decl) noexcept { return decl->get_self_ty(); });
  };

  [[nodiscard]] auto opaque_self() const noexcept -> bool {
    return visit([](Fn* fn) noexcept { return fn->abstract() || fn->has_annotation(ast::FnDecl::ANN_OVERRIDE); },
                 [](auto&& /* default */) noexcept { return false; });
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
  /// Whether the local scope "owns" the variable. Unowned variables are not destructed at the end of the scope.
  bool owning{};
};

/// A source file with its associated Syntax Tree.
struct SourceFile {
  fs::path path;
  string name;
  vector<yume::Token> tokens;
  ast::TokenIterator iterator;
  unique_ptr<ast::Program> program;

  static auto name_or_stdin(const fs::path& path) -> string { return path.empty() ? "<stdin>"s : path.native(); }

  SourceFile(std::istream& in, fs::path path)
      : path(move(path)), name(name_or_stdin(this->path)),
        tokens(yume::tokenize(in, this->name)), iterator{tokens.begin(), tokens.end()} {
#ifdef YUME_SPEW_LIST_TOKENS
    llvm::outs() << "tokens:\n";
    for (auto& i : tokens)
      llvm::outs() << "  " << i << "\n";
    llvm::outs() << "\n";
    llvm::outs().flush();
#endif
    program = ast::Program::parse(iterator);
  }
};
} // namespace yume
