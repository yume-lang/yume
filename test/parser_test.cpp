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

template <typename T = CallExpr, typename E = Expr, typename N, typename... Ts>
auto make_call(N&& name, Ts&&... ts) -> std::unique_ptr<T> {
  return ast<T>(std::forward<N>(name), ptr_vec<E>(std::forward<Ts>(ts)...));
}

auto operator""_Var(const char* str, size_t size) { return ast<VarExpr>(std::string{str, size}); }
auto operator""_Num(unsigned long long val) { return ast<NumberExpr>(val); }
auto operator""_String(const char* str, size_t size) { return ast<StringExpr>(std::string{str, size}); }
auto operator""_Char(char chr) { return ast<CharExpr>(chr); }
auto operator""_Type(const char* str, size_t size) { return ast<SimpleType>(std::string{str, size}); }

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
#define CHECK_PARSER_THROWS(body) CHECK_THROWS(*prog(body))

using namespace yume::ast;
using enum yume::Qualifier;

TEST_CASE("Parse literals", "[parse]") {
  CHECK_PARSER("true", ast<BoolExpr>(true));
  CHECK_PARSER("false", ast<BoolExpr>(false));

  CHECK_PARSER("1", 1_Num);

  CHECK_PARSER("?a", 'a'_Char);

  CHECK_PARSER(R"("hi")", "hi"_String);
}

TEST_CASE("Parse operator calls", "[parse]") {
  CHECK_PARSER("1 + 2", make_call("+", 1_Num, 2_Num));
  CHECK_PARSER("1 +2", make_call("+", 1_Num, 2_Num));
  CHECK_PARSER("1 - 2", make_call("-", 1_Num, 2_Num));
  CHECK_PARSER("1 -2", make_call("-", 1_Num, 2_Num));
  CHECK_PARSER("-2", make_call("-", 2_Num));
  CHECK_PARSER("!true", make_call("!", ast<BoolExpr>(true)));
}

TEST_CASE("Parse operator precedence", "[parse]") {
  CHECK_PARSER("1 + 2 * 3 == 6 + 1",
               make_call("==", make_call("+", 1_Num, make_call("*", 2_Num, 3_Num)), make_call("+", 6_Num, 1_Num)));
}

TEST_CASE("Parse assignment", "[parse]") {
  CHECK_PARSER("a = 1\nb = 2", ast<AssignExpr>("a"_Var, 1_Num), ast<AssignExpr>("b"_Var, 2_Num));
  CHECK_PARSER("a = b = 2", ast<AssignExpr>("a"_Var, ast<AssignExpr>("b"_Var, 2_Num)));
}

TEST_CASE("Parse direct calling", "[parse]") {
  CHECK_PARSER("a", "a"_Var);
  CHECK_PARSER("(a)", "a"_Var);
  CHECK_PARSER("a()", make_call("a"));
  CHECK_PARSER("a(1, 2, 3)", make_call("a", 1_Num, 2_Num, 3_Num));
  CHECK_PARSER("a(b(1), c(2, 3))", make_call("a", make_call("b", 1_Num), make_call("c", 2_Num, 3_Num)));
}

// TODO:
TEST_CASE("Parse indirect calling", "[!shouldfail][parse]") { CHECK_PARSER("(a)()", make_call("a")); }

TEST_CASE("Parse member calling", "[parse]") {
  CHECK_PARSER("receiver.call", make_call("call", "receiver"_Var));
  CHECK_PARSER("receiver.call()", make_call("call", "receiver"_Var));
  CHECK_PARSER("receiver.call(1, 2, 3)", make_call("call", "receiver"_Var, 1_Num, 2_Num, 3_Num));
}

TEST_CASE("Parse setter calling", "[parse]") {
  CHECK_PARSER("receiver.foo = 0", make_call("foo=", "receiver"_Var, 0_Num));
  CHECK_PARSER("r.f = r", make_call("f=", "r"_Var, "r"_Var));
}

TEST_CASE("Parse direct field access", "[parse]") {
  CHECK_PARSER("widget::field", ast<FieldAccessExpr>("widget"_Var, "field"));
  CHECK_PARSER("widget::field.run", make_call("run", ast<FieldAccessExpr>("widget"_Var, "field")));

  CHECK_PARSER("widget::field = 0", ast<AssignExpr>(ast<FieldAccessExpr>("widget"_Var, "field"), 0_Num));
}

TEST_CASE("Parse constructor calling", "[parse]") {
  CHECK_PARSER("Type()", make_call<CtorExpr>("Type"_Type));
  CHECK_PARSER("Type(a)", make_call<CtorExpr>("Type"_Type, "a"_Var));

  CHECK_PARSER("Type[]()", make_call<CtorExpr>(ast<QualType>("Type"_Type, Slice)));
  CHECK_PARSER("Type[](a)", make_call<CtorExpr>(ast<QualType>("Type"_Type, Slice), "a"_Var));
}

TEST_CASE("Parse slice literal", "[parse]") {
  CHECK_PARSER("I32[1, 2, 3]", make_call<SliceExpr>("I32"_Type, 1_Num, 2_Num, 3_Num));
}

// TODO:
TEST_CASE("Parse empty slice literal", "[!shouldfail][parse]") {
  CHECK_PARSER("I32[]", make_call<SliceExpr>("I32"_Type));
}

TEST_CASE("Parse bare type", "[parse][throws]") {
  CHECK_PARSER_THROWS("T ptr");
  CHECK_PARSER_THROWS("I32");
}

TEST_CASE("Parse index operator", "[parse]") {
  CHECK_PARSER("a[b]", make_call("[]", "a"_Var, "b"_Var));
  CHECK_PARSER("a[b] = c", make_call("[]=", "a"_Var, "b"_Var, "c"_Var));
}

TEST_CASE("Parse variable declaration", "[parse]") {
  CHECK_PARSER("let a = 0", ast<VarDecl>("a", std::nullopt, 0_Num));
  CHECK_PARSER("let a I32 = 0", ast<VarDecl>("a", "I32"_Type, 0_Num));
}

#undef CHECK_PARSER
#undef CHECK_PARSER_THROWS
