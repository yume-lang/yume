#include "./test_common.hpp"
#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "diagnostic/visitor/hash_visitor.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <utility>

namespace {
using namespace yume::ast;

auto prog(const std::string& str) -> std::unique_ptr<Program> {
  static const std::string TEST_FILENAME = "<test>";
  auto in_stream = std::stringstream(str);
  auto tokens = yume::tokenize(in_stream, TEST_FILENAME);
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

template <typename E = Expr, typename N, typename... Ts>
auto make_ctor(N&& type, std::string name, Ts&&... ts) -> std::unique_ptr<CtorExpr> {
  return ast<CtorExpr>(std::forward<N>(type), name, ptr_vec<E>(std::forward<Ts>(ts)...));
}

template <typename... Ts> auto make_compound(Ts&&... ts) -> Compound {
  return std::move(*ast<Compound>(ptr_vec<Stmt>(std::forward<Ts>(ts)...)));
}

struct TName {
  mutable std::unique_ptr<Type> type; // because initializer lists always add const
  std::string name;

  TName(std::unique_ptr<Type> type, std::string name) : type(std::move(type)), name(std::move(name)) {}
  TName(std::string name, std::unique_ptr<Type> type) : type(std::move(type)), name(std::move(name)) {}

  explicit operator TypeName() const { return std::move(*ast<TypeName>(std::move(type), name)); }
};

template <typename... Ts>
auto make_fn_decl(const std::string& name, std::initializer_list<TName> args = {},
                  std::vector<std::string> type_args = {}, std::optional<std::unique_ptr<Type>> ret = std::nullopt,
                  Ts&&... ts) -> std::unique_ptr<FnDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<FnDecl>(name, std::move(ast_args), std::move(type_args), std::move(ret),
                     make_compound(std::forward<Ts>(ts)...));
}

auto make_fn_decl(const std::string& name, std::initializer_list<TName> args, std::optional<std::unique_ptr<Type>> ret,
                  const std::string& primitive, bool varargs = false) -> std::unique_ptr<FnDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<FnDecl>(name, std::move(ast_args), std::vector<std::string>{}, std::move(ret), varargs, primitive);
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

template <typename... Ts> auto equals_ast(Ts&&... ts) {
  std::vector<std::unique_ptr<yume::ast::AST>> vec{};
  vec.reserve(sizeof...(ts));
  (vec.emplace_back(std::forward<Ts>(ts)), ...);
  return EqualsDirectMatcher<decltype(vec), decltype(ast_comparison), decltype(deref)>{std::move(vec)};
}
} // namespace

namespace yume::ast {
auto operator&(std::unique_ptr<Type> type, Qualifier qual) { return ::ast<QualType>(std::move(type), qual); }
} // namespace yume::ast

#define CHECK_PARSER(body, ...) CHECK_THAT(*prog(body), equals_ast(__VA_ARGS__))
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

// TODO(rymiel)
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
  CHECK_PARSER("Type()", make_ctor("Type"_Type, "new"));
  CHECK_PARSER("Type(a)", make_ctor("Type"_Type, "new", "a"_Var));

  CHECK_PARSER("Type:custom()", make_ctor("Type"_Type, "custom"));
  CHECK_PARSER("Type:Custom(a)", make_ctor("Type"_Type, "Custom", "a"_Var));

  CHECK_PARSER("Type[]()", make_ctor("Type"_Type & Slice, "new"));
  CHECK_PARSER("Type[](a)", make_ctor("Type"_Type & Slice, "new", "a"_Var));
}

TEST_CASE("Parse slice literal", "[parse]") {
  CHECK_PARSER("I32:[1, 2, 3]", make_call<SliceExpr>("I32"_Type, 1_Num, 2_Num, 3_Num));
  CHECK_PARSER("I32:[]", make_call<SliceExpr>("I32"_Type));
}

TEST_CASE("Parse bare type", "[parse][throws]") {
  CHECK_PARSER_THROWS("T ptr");
  CHECK_PARSER_THROWS("I32");
  CHECK_PARSER_THROWS("I32[]");
}

TEST_CASE("Parse index operator", "[parse]") {
  CHECK_PARSER("a[b]", make_call("[]", "a"_Var, "b"_Var));
  CHECK_PARSER("a[b] = c", make_call("[]=", "a"_Var, "b"_Var, "c"_Var));
}

TEST_CASE("Parse variable declaration", "[parse]") {
  CHECK_PARSER("let a = 0", ast<VarDecl>("a", std::nullopt, 0_Num));
  CHECK_PARSER("let a I32 = 0", ast<VarDecl>("a", "I32"_Type, 0_Num));
}

TEST_CASE("Parse function declaration", "[parse][fn]") {
  CHECK_PARSER("def foo()\nend", make_fn_decl("foo"));
  CHECK_PARSER("def foo(a I32)\nend", make_fn_decl("foo", {{"a", "I32"_Type}}));
  CHECK_PARSER("def bar(a I32, x Foo)\nend", make_fn_decl("bar", {{"a", "I32"_Type}, {"x", "Foo"_Type}}));
  CHECK_PARSER("def bar() U32\nend", make_fn_decl("bar", {}, {}, "U32"_Type));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               make_fn_decl("baz", {{"l", "Left"_Type}, {"r", "Right"_Type}}, {}, "U32"_Type));

  CHECK_PARSER("def foo(a I32) I32\na\nend", make_fn_decl("foo", {{"a", "I32"_Type}}, {}, "I32"_Type, "a"_Var));
}

TEST_CASE("Parse short function declaration", "[parse][fn]") {
  CHECK_PARSER("def short() = 0", make_fn_decl("short", {}, {}, {}, ast<ReturnStmt>(0_Num)));
  CHECK_PARSER("def short(a I32) = a", make_fn_decl("short", {{"a", "I32"_Type}}, {}, {}, ast<ReturnStmt>("a"_Var)));

  CHECK_PARSER("def short(a I32) I32 = a",
               make_fn_decl("short", {{"a", "I32"_Type}}, {}, "I32"_Type, ast<ReturnStmt>("a"_Var)));

  CHECK_PARSER("def short(a I32, b I32) I32 = a + b",
               make_fn_decl("short", {{"a", "I32"_Type}, {"b", "I32"_Type}}, {}, "I32"_Type,
                            ast<ReturnStmt>(make_call("+", "a"_Var, "b"_Var))));
}

TEST_CASE("Parse templated function declaration", "[parse][fn]") {
  CHECK_PARSER("def templated{T}()\nend", make_fn_decl("templated", {}, {"T"}, {}));

  CHECK_PARSER("def templated{T}(t T) T\nend", make_fn_decl("templated", {{"t", "T"_Type}}, {"T"}, "T"_Type));

  CHECK_PARSER("def templated{T}(t T) T = t",
               make_fn_decl("templated", {{"t", "T"_Type}}, {"T"}, "T"_Type, ast<ReturnStmt>("t"_Var)));

  CHECK_PARSER("def templated{T,U,V,X,Y,Z}(t T, u U, v V) X = Y() + Z:z()",
               make_fn_decl("templated", {{"t", "T"_Type}, {"u", "U"_Type}, {"v", "V"_Type}},
                            {"T", "U", "V", "X", "Y", "Z"}, "X"_Type,
                            ast<ReturnStmt>(make_call("+", make_ctor("Y"_Type, "new"), make_ctor("Z"_Type, "z")))));

  CHECK_PARSER("def templated{T}(slice T[], pointer T ptr, mutable T mut, mix T ptr[] mut)\nend",
               make_fn_decl("templated",
                            {{"slice", "T"_Type & Slice},
                             {"pointer", "T"_Type & Ptr},
                             {"mutable", "T"_Type & Mut},
                             {"mix", "T"_Type & Ptr & Slice & Mut}},
                            {"T"}, {}));
}

TEST_CASE("Parse operator function declaration", "[parse][fn]") {
  CHECK_PARSER("def +()\nend", make_fn_decl("+"));

  CHECK_PARSER("def -() = 0", make_fn_decl("-", {}, {}, {}, ast<ReturnStmt>(0_Num)));

  CHECK_PARSER("def !(a I32) I32\nend", make_fn_decl("!", {{"a", "I32"_Type}}, {}, "I32"_Type));

  CHECK_PARSER("def [](a I32[], x Foo)\nend", make_fn_decl("[]", {{"a", "I32"_Type & Slice}, {"x", "Foo"_Type}}));

  CHECK_PARSER(
      "def []=(target L, offs I32, val V) L[]\nend",
      make_fn_decl("[]=", {{"target", "L"_Type}, {"offs", "I32"_Type}, {"val", "V"_Type}}, {}, "L"_Type & Slice));

  CHECK_PARSER("def +=(i I32 mut, j I32) I32 mut = i + j",
               make_fn_decl("+=", {{"i", "I32"_Type & Mut}, {"j", "I32"_Type}}, {}, "I32"_Type & Mut,
                            ast<ReturnStmt>(make_call("+", "i"_Var, "j"_Var))));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               make_fn_decl("baz", {{"l", "Left"_Type}, {"r", "Right"_Type}}, {}, "U32"_Type));

  CHECK_PARSER("def foo(a I32) I32\na\nend", make_fn_decl("foo", {{"a", "I32"_Type}}, {}, "I32"_Type, "a"_Var));
}

TEST_CASE("Parse primitive function declaration", "[parse][fn]") {
  CHECK_PARSER("def prim() = __primitive__(name_of_primitive)", make_fn_decl("prim", {}, {}, "name_of_primitive"));
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = __primitive__(native_munge)",
               make_fn_decl("munge", {{"a", "I32"_Type & Ptr}}, "I32"_Type & Ptr, "native_munge"));
  CHECK_PARSER("def printf(format U8 ptr) = __primitive__(libc_printf) __varargs__",
               make_fn_decl("printf", {{"format", "U8"_Type & Ptr}}, {}, "libc_printf", true));
}

#undef CHECK_PARSER
#undef CHECK_PARSER_THROWS
