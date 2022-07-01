#include "./test_common.hpp"
#include "ast/ast.hpp"
#include "token.hpp"
#include "util.hpp"
#include "visitor/hash_visitor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <iterator>

namespace {
using namespace yume::ast;

auto prog(const std::string& str) -> std::unique_ptr<Program> {
  static const std::string test_filename = "<test>";
  auto in_stream = std::stringstream(str);
  auto tokens = yume::tokenize(in_stream, test_filename);
  auto iter = TokenIterator{tokens.begin(), tokens.end()};
  return Program::parse(iter);
}

template <typename T, typename... Ts> auto ast(Ts&&... ts) -> std::unique_ptr<T> {
  return std::make_unique<T>(std::span<yume::Token>{}, std::forward<Ts>(ts)...);
}

template <typename T, typename... Ts> auto ptr_vec(Ts&&... ts) {
  std::vector<std::unique_ptr<T>> vec{};
  vec.reserve(sizeof...(ts));
  (vec.emplace_back(std::forward<Ts>(ts)), ...);
  return vec;
}

auto operator""_Var(const char* str, size_t size) { return ast<VarExpr>(std::string{str, size}); }
auto operator""_Num(unsigned long long val) { return ast<NumberExpr>(val); }
auto operator""_String(const char* str, size_t size) { return ast<StringExpr>(std::string{str, size}); }
auto operator""_Char(char chr) { return ast<CharExpr>(chr); }

constexpr auto ast_comparison = [](const yume::ast::Program& a,
                                   const std::vector<std::unique_ptr<yume::ast::AST>>& b) -> bool {
  uint64_t expected_seed = 0;
  uint64_t actual_seed = 0;
  {
    yume::diagnostic::HashVisitor visitor(expected_seed);
    for (const auto& i : a.body())
      visitor.visit(i, nullptr);
  }
  {
    yume::diagnostic::HashVisitor visitor(actual_seed);
    for (const auto& i : b)
      visitor.visit(*i, nullptr);
  }
  return expected_seed == actual_seed;
};

constexpr auto deref = [](const auto& ptr_range) { return yume::dereference_view(ptr_range); };

template <typename... Ts> auto EqualsAST(Ts&&... ts) {
  std::vector<std::unique_ptr<yume::ast::AST>> vec{};
  vec.reserve(sizeof...(ts));
  (vec.emplace_back(std::forward<Ts>(ts)), ...);
  return EqualsDirectMatcher<decltype(vec), decltype(ast_comparison), decltype(deref)>{std::move(vec)};
}
} // namespace

#define CHECK_PARSER(body, ...) CHECK_THAT(*prog(body), EqualsAST(__VA_ARGS__))

TEST_CASE("Parsing", "[parse]") {
  using namespace yume::ast;

  CHECK_PARSER("true", ast<BoolExpr>(true));
  CHECK_PARSER("false", ast<BoolExpr>(false));

  CHECK_PARSER("1", 1_Num);

  CHECK_PARSER("?a", 'a'_Char);

  CHECK_PARSER(R"("hi")", "hi"_String);

  CHECK_PARSER("1 + 2", ast<CallExpr>("+", ptr_vec<Expr>(1_Num, 2_Num)));
  CHECK_PARSER("1 +2", ast<CallExpr>("+", ptr_vec<Expr>(1_Num, 2_Num)));
  CHECK_PARSER("1 - 2", ast<CallExpr>("-", ptr_vec<Expr>(1_Num, 2_Num)));
  CHECK_PARSER("1 -2", ast<CallExpr>("-", ptr_vec<Expr>(1_Num, 2_Num)));
  CHECK_PARSER("-2", ast<CallExpr>("-", ptr_vec<Expr>(2_Num)));
  CHECK_PARSER("!true", ast<CallExpr>("!", ptr_vec<Expr>(ast<BoolExpr>(true))));

  CHECK_PARSER("a = 1\nb = 2", ast<AssignExpr>("a"_Var, 1_Num), ast<AssignExpr>("b"_Var, 2_Num));
  CHECK_PARSER("a = b = 2", ast<AssignExpr>("a"_Var, ast<AssignExpr>("b"_Var, 2_Num)));

  CHECK_PARSER("a", "a"_Var);
  CHECK_PARSER("(a)", "a"_Var);
  CHECK_PARSER("a()", ast<CallExpr>("a", ptr_vec<Expr>()));
  // CHECK_PARSER("(a)()", ast<CallExpr>("a", ptr_vec<Expr>()));

  CHECK_PARSER("a[b]", ast<CallExpr>("[]", ptr_vec<Expr>("a"_Var, "b"_Var)));
  CHECK_PARSER("a[b] = c", ast<CallExpr>("[]=", ptr_vec<Expr>("a"_Var, "b"_Var, "c"_Var)));
}

#undef CHECK_PARSER
