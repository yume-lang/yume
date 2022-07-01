#include "./test_common.hpp"
#include "ast/ast.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <iterator>

namespace {
auto prog(const std::string& str) -> std::unique_ptr<yume::ast::Program> {
  static const std::string test_filename = "<test>";
  auto in_stream = std::stringstream(str);
  auto tokens = yume::tokenize(in_stream, test_filename);
  auto iter = yume::ast::TokenIterator{tokens.begin(), tokens.end()};
  return yume::ast::Program::parse(iter);
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

constexpr auto ast_comparison = [](const yume::ast::Program& a,
                                   const std::vector<std::unique_ptr<yume::ast::AST>>& b) -> bool {
  std::string expected_str;                     // HACK
  std::string actual_str;                       // HACK
  {                                             // HACK
    llvm::raw_string_ostream ss(expected_str);  // HACK
    yume::diagnostic::PrintVisitor visitor(ss); // HACK
    for (const auto& i : a.body())              // HACK
      visitor.visit(i, nullptr);                // HACK
  }                                             // HACK
  {                                             // HACK
    llvm::raw_string_ostream ss(actual_str);    // HACK
    yume::diagnostic::PrintVisitor visitor(ss); // HACK
    for (const auto& i : b)                     // HACK
      visitor.visit(*i, nullptr);               // HACK
  }                                             // HACK
  return expected_str == actual_str;            // HACK
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

  CHECK_PARSER("1", ast<NumberExpr>(1));

  CHECK_PARSER("?a", ast<CharExpr>('a'));

  CHECK_PARSER(R"("hi")", ast<StringExpr>("hi"));

  CHECK_PARSER("1 + 2", ast<CallExpr>("+", ptr_vec<Expr>(ast<NumberExpr>(1), ast<NumberExpr>(2))));
  CHECK_PARSER("1 +2", ast<CallExpr>("+", ptr_vec<Expr>(ast<NumberExpr>(1), ast<NumberExpr>(2))));
  CHECK_PARSER("1 - 2", ast<CallExpr>("-", ptr_vec<Expr>(ast<NumberExpr>(1), ast<NumberExpr>(2))));
  CHECK_PARSER("1 -2", ast<CallExpr>("-", ptr_vec<Expr>(ast<NumberExpr>(1), ast<NumberExpr>(2))));
  CHECK_PARSER("-2", ast<CallExpr>("-", ptr_vec<Expr>(ast<NumberExpr>(2))));
  CHECK_PARSER("!true", ast<CallExpr>("!", ptr_vec<Expr>(ast<BoolExpr>(true))));

  CHECK_PARSER("a = 1\nb = 2", ast<AssignExpr>(ast<VarExpr>("a"), ast<NumberExpr>(1)),
               ast<AssignExpr>(ast<VarExpr>("b"), ast<NumberExpr>(2)));
  CHECK_PARSER("a = b = 2", ast<AssignExpr>(ast<VarExpr>("a"), ast<AssignExpr>(ast<VarExpr>("b"), ast<NumberExpr>(2))));

  CHECK_PARSER("a[b]", ast<CallExpr>("[]", ptr_vec<Expr>(ast<VarExpr>("a"), ast<VarExpr>("b"))));
  CHECK_PARSER("a[b] = c",
               ast<CallExpr>("[]=", ptr_vec<Expr>(ast<VarExpr>("a"), ast<VarExpr>("b"), ast<VarExpr>("c"))));
}

#undef CHECK_PARSER
