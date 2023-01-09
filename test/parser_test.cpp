#include "./test_common.hpp"
#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "atom.hpp"
#include "diagnostic/notes.hpp"
#include "diagnostic/visitor/hash_visitor.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace {
using namespace yume::ast;

auto prog(const std::string& str) -> std::unique_ptr<Program> {
  static const std::string TEST_FILENAME = "<test>";
  auto in_stream = std::stringstream(str);
  auto tokens = yume::tokenize(in_stream, TEST_FILENAME);
  auto iter = TokenIterator{tokens.begin(), tokens.end()};
  auto notes = yume::diagnostic::NotesHolder{};
  // TODO(rymiel): Maybe make some sort of special notesholder which fails the test if a diagnostic is emitted
  return Program::parse(iter, notes);
}

template <typename T, typename... Ts> auto ast(Ts&&... ts) -> std::unique_ptr<T> {
  return std::make_unique<T>(std::span<yume::Token>{}, std::forward<Ts>(ts)...);
}

template <typename T, typename... Ts> auto ptr_vec(Ts&&... ts) {
  std::vector<yume::ast::AnyBase<T>> vec{};
  vec.reserve(sizeof...(ts));
  (vec.emplace_back(std::forward<Ts>(ts)), ...);
  return vec;
}

template <typename... Ts> auto make_call(std::string name, Ts&&... ts) -> std::unique_ptr<CallExpr> {
  return ast<CallExpr>(std::move(name), std::nullopt, ptr_vec<Expr>(std::forward<Ts>(ts)...));
}

template <typename... Ts>
auto make_recv_call(std::string name, std::unique_ptr<Type> type, Ts&&... ts) -> std::unique_ptr<CallExpr> {
  return ast<CallExpr>(std::move(name), std::move(type), ptr_vec<Expr>(std::forward<Ts>(ts)...));
}

template <typename E = Expr, typename N, typename... Ts>
auto make_ctor(N type, Ts&&... ts) -> std::unique_ptr<CtorExpr> {
  return ast<CtorExpr>(AnyType(std::move(type)), ptr_vec<E>(std::forward<Ts>(ts)...));
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

struct FnDeclArgs {
  std::string name;
  std::initializer_list<TName> args = {};
  std::vector<std::string> type_args = {};
  OptionalType ret = std::nullopt;
  std::set<std::string> attributes = {};
};

template <typename... Ts> auto make_fn_decl(FnDeclArgs a, Ts&&... ts) -> std::unique_ptr<FnDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(a.args.size());
  std::transform(a.args.begin(), a.args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<FnDecl>(a.name, std::move(ast_args), std::move(a.type_args), std::move(a.ret),
                     make_compound(std::forward<Ts>(ts)...), std::move(a.attributes));
}

auto make_primitive_decl(const std::string& name, std::initializer_list<TName> args, OptionalType ret,
                         const std::string& primitive, std::set<std::string> attributes = {})
    -> std::unique_ptr<FnDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<FnDecl>(name, std::move(ast_args), std::vector<std::string>{}, std::move(ret), primitive,
                     std::move(attributes));
}

auto make_extern_decl(const std::string& name, std::initializer_list<TName> args, OptionalType ret,
                      const std::string& extern_name, bool varargs = false) -> std::unique_ptr<FnDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<FnDecl>(name, std::move(ast_args), std::vector<std::string>{}, std::move(ret),
                     FnDecl::extern_decl_t{extern_name, varargs}, std::set<std::string>{});
}

auto make_abstract_decl(const std::string& name, std::initializer_list<TName> args, OptionalType ret,
                        std::set<std::string> attributes = {}) -> std::unique_ptr<FnDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<FnDecl>(name, std::move(ast_args), std::vector<std::string>{}, std::move(ret), FnDecl::abstract_decl_t{},
                     std::move(attributes));
}

template <typename... Ts>
auto make_struct_decl(const std::string& name, std::initializer_list<TName> args, std::vector<std::string> type_vars,
                      std::unique_ptr<Type> implements, Ts&&... ts) -> std::unique_ptr<StructDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<StructDecl>(name, std::move(ast_args), type_vars, make_compound(std::forward<Ts>(ts)...), move(implements),
                         false);
}

template <typename... Ts>
auto make_interface_decl(const std::string& name, std::initializer_list<TName> args, std::vector<std::string> type_vars,
                         Ts&&... ts) -> std::unique_ptr<StructDecl> {
  auto ast_args = std::vector<TypeName>();
  ast_args.reserve(args.size());
  std::transform(args.begin(), args.end(), std::back_inserter(ast_args),
                 [](auto& tn) { return static_cast<TypeName>(tn); });
  return ast<StructDecl>(name, std::move(ast_args), type_vars, make_compound(std::forward<Ts>(ts)...), std::nullopt,
                         true);
}

auto operator""_Var(const char* str, size_t size) { return ast<VarExpr>(std::string{str, size}); }
auto operator""_Num(unsigned long long val) { return ast<NumberExpr>(val); }
auto operator""_String(const char* str, size_t size) { return ast<StringExpr>(std::string{str, size}); }
auto operator""_Char(char chr) { return ast<CharExpr>(chr); }
auto operator""_Type(const char* str, size_t size) { return ast<SimpleType>(std::string{str, size}); }
auto Self() { return ast<SelfType>(); } // NOLINT(readability-identifier-naming)

constexpr auto ast_comparison = [](const yume::ast::Program& a,
                                   const std::vector<std::unique_ptr<yume::ast::AST>>& b) -> bool {
  uint64_t expected_seed = 0;
  uint64_t actual_seed = 0;
  {
    yume::diagnostic::HashVisitor visitor(expected_seed);
    for (const auto& i : a.body)
      visitor.visit(*i, "");
  }
  {
    yume::diagnostic::HashVisitor visitor(actual_seed);
    for (const auto& i : b)
      visitor.visit(*i, "");
  }
  return expected_seed == actual_seed;
};

template <typename... Ts> auto equals_ast(Ts&&... ts) {
  std::vector<std::unique_ptr<yume::ast::AST>> vec{};
  vec.reserve(sizeof...(ts));
  (vec.emplace_back(std::forward<Ts>(ts)), ...);
  return EqualsDirectMatcher<decltype(vec), decltype(ast_comparison)>{std::move(vec)};
}
} // namespace

namespace yume::ast {

struct {
} Slice;

auto operator&(std::unique_ptr<Type> type, Qualifier qual) { return ::ast<QualType>(std::move(type), qual); }
auto operator&(std::unique_ptr<Type> type, decltype(Slice) /* tag */) {
  std::vector<yume::ast::AnyType> type_args = {};
  type_args.emplace_back(std::move(type));
  return ::ast<TemplatedType>(::ast<SimpleType>("Slice"), std::move(type_args));
}
} // namespace yume::ast

#define CHECK_PARSER(body, ...) CHECK_THAT(*prog(body), equals_ast(__VA_ARGS__))
#define CHECK_PARSER_THROWS(body) CHECK_THROWS(*prog(body))

using namespace yume::ast;
using enum yume::Qualifier;
using yume::operator""_a;

TEST_CASE("Parse literals", "[parse]") {
  CHECK_PARSER("true", ast<BoolExpr>(true));
  CHECK_PARSER("false", ast<BoolExpr>(false));

  CHECK_PARSER("1", 1_Num);
  CHECK_PARSER("42", 42_Num);
  CHECK_PARSER("0x0", 0_Num);
  CHECK_PARSER("0x0fc", 0xfc_Num);
  CHECK_PARSER("0x2fc", 0x2fc_Num);

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

TEST_CASE("Parse member calling", "[parse]") {
  CHECK_PARSER("receiver.call", make_call("call", "receiver"_Var));
  CHECK_PARSER("receiver.call()", make_call("call", "receiver"_Var));
  CHECK_PARSER("receiver.call(1, 2, 3)", make_call("call", "receiver"_Var, 1_Num, 2_Num, 3_Num));
}

TEST_CASE("Parse receiver calling", "[parse]") {
  CHECK_PARSER("Foo.call", make_recv_call("call", "Foo"_Type));
  CHECK_PARSER("Foo.call()", make_recv_call("call", "Foo"_Type));
  CHECK_PARSER("Foo.call(1, 2, 3)", make_recv_call("call", "Foo"_Type, 1_Num, 2_Num, 3_Num));
  CHECK_PARSER("Foo[].call()", make_recv_call("call", "Foo"_Type & Slice));
  CHECK_PARSER("Foo mut.call()", make_recv_call("call", "Foo"_Type & Mut));
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
  CHECK_PARSER("Type()", make_ctor("Type"_Type));
  CHECK_PARSER("Type(a)", make_ctor("Type"_Type, "a"_Var));

  CHECK_PARSER("Type[]()", make_ctor("Type"_Type & Slice));
  CHECK_PARSER("Type[](a)", make_ctor("Type"_Type & Slice, "a"_Var));
}

TEST_CASE("Parse slice literal", "[parse]") {
  CHECK_PARSER("I32:[1, 2, 3]", ast<SliceExpr>("I32"_Type, ptr_vec<Expr>(1_Num, 2_Num, 3_Num)));
  CHECK_PARSER("I32:[]", ast<SliceExpr>("I32"_Type, ptr_vec<Expr>()));
}

TEST_CASE("Parse type expression", "[parse]") {
  CHECK_PARSER("T ptr", ast<TypeExpr>("T"_Type & Ptr));
  CHECK_PARSER("I32", ast<TypeExpr>("I32"_Type));
  CHECK_PARSER("I32[]", ast<TypeExpr>("I32"_Type & Slice));
  CHECK_PARSER("0.as(I64)", make_call("as", 0_Num, ast<TypeExpr>("I64"_Type)));
}

TEST_CASE("Parse index operator", "[parse]") {
  CHECK_PARSER("a[b]", make_call("[]", "a"_Var, "b"_Var));
  CHECK_PARSER("a[b] = c", make_call("[]=", "a"_Var, "b"_Var, "c"_Var));
}

TEST_CASE("Parse logical operators", "[parse]") {
  CHECK_PARSER("a || b", ast<BinaryLogicExpr>("||"_a, "a"_Var, "b"_Var));
  CHECK_PARSER("a && b", ast<BinaryLogicExpr>("&&"_a, "a"_Var, "b"_Var));
  CHECK_PARSER("a || b || c", ast<BinaryLogicExpr>("||"_a, "a"_Var, ast<BinaryLogicExpr>("||"_a, "b"_Var, "c"_Var)));
  CHECK_PARSER("a && b && c", ast<BinaryLogicExpr>("&&"_a, "a"_Var, ast<BinaryLogicExpr>("&&"_a, "b"_Var, "c"_Var)));
  CHECK_PARSER("a && b || c && d", ast<BinaryLogicExpr>("||"_a, ast<BinaryLogicExpr>("&&"_a, "a"_Var, "b"_Var),
                                                        ast<BinaryLogicExpr>("&&"_a, "c"_Var, "d"_Var)));
  CHECK_PARSER(
      "a || b && c || d",
      ast<BinaryLogicExpr>("||"_a, "a"_Var,
                           ast<BinaryLogicExpr>("||"_a, ast<BinaryLogicExpr>("&&"_a, "b"_Var, "c"_Var), "d"_Var)));
}

TEST_CASE("Parse variable declaration", "[parse]") {
  CHECK_PARSER("let a = 0", ast<VarDecl>("a", std::nullopt, 0_Num));
  CHECK_PARSER("let a I32 = 0", ast<VarDecl>("a", "I32"_Type, 0_Num));
}

TEST_CASE("Parse function declaration", "[parse][fn]") {
  CHECK_PARSER("def foo()\nend", make_fn_decl({"foo"}));
  CHECK_PARSER("def foo(a I32)\nend", make_fn_decl({"foo", {{"a", "I32"_Type}}}));
  CHECK_PARSER("def bar(a I32, x Foo)\nend", make_fn_decl({"bar", {{"a", "I32"_Type}, {"x", "Foo"_Type}}}));
  CHECK_PARSER("def bar() U32\nend", make_fn_decl({.name = "bar", .ret = "U32"_Type}));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               make_fn_decl({.name = "baz", .args = {{"l", "Left"_Type}, {"r", "Right"_Type}}, .ret = "U32"_Type}));

  CHECK_PARSER("def foo(a I32) I32\na\nend",
               make_fn_decl({.name = "foo", .args = {{"a", "I32"_Type}}, .ret = "I32"_Type}, "a"_Var));
}

TEST_CASE("Parse short function declaration", "[parse][fn]") {
  CHECK_PARSER("def short() = 0", make_fn_decl({"short"}, ast<ReturnStmt>(0_Num)));
  CHECK_PARSER("def short(a I32) = a", make_fn_decl({"short", {{"a", "I32"_Type}}}, ast<ReturnStmt>("a"_Var)));

  CHECK_PARSER(
      "def short(a I32) I32 = a",
      make_fn_decl({.name = "short", .args = {{"a", "I32"_Type}}, .ret = "I32"_Type}, ast<ReturnStmt>("a"_Var)));

  CHECK_PARSER("def short(a I32, b I32) I32 = a + b",
               make_fn_decl({.name = "short", .args = {{"a", "I32"_Type}, {"b", "I32"_Type}}, .ret = "I32"_Type},
                            ast<ReturnStmt>(make_call("+", "a"_Var, "b"_Var))));
}

TEST_CASE("Parse templated function declaration", "[parse][fn]") {
  CHECK_PARSER("def templated{T}()\nend", make_fn_decl({.name = "templated", .type_args = {"T"}}));

  CHECK_PARSER("def templated{T}(t T) T\nend",
               make_fn_decl({.name = "templated", .args = {{"t", "T"_Type}}, .type_args = {"T"}, .ret = "T"_Type}));

  CHECK_PARSER("def templated{T}(t T) T = t",
               make_fn_decl({.name = "templated", .args = {{"t", "T"_Type}}, .type_args = {"T"}, .ret = "T"_Type},
                            ast<ReturnStmt>("t"_Var)));

  CHECK_PARSER("def templated{T,U,V,X,Y,Z}(t T, u U, v V) X = Y() + Z()",
               make_fn_decl({.name = "templated",
                             .args = {{"t", "T"_Type}, {"u", "U"_Type}, {"v", "V"_Type}},
                             .type_args = {"T", "U", "V", "X", "Y", "Z"},
                             .ret = "X"_Type},
                            ast<ReturnStmt>(make_call("+", make_ctor("Y"_Type), make_ctor("Z"_Type)))));

  CHECK_PARSER("def templated{T}(slice T[], pointer T ptr, mutable T mut, mix T ptr[] mut)\nend",
               make_fn_decl({.name = "templated",
                             .args = {{"slice", "T"_Type & Slice},
                                      {"pointer", "T"_Type & Ptr},
                                      {"mutable", "T"_Type & Mut},
                                      {"mix", "T"_Type & Ptr & Slice & Mut}},
                             .type_args{"T"}}));
}

TEST_CASE("Parse operator function declaration", "[parse][fn]") {
  CHECK_PARSER("def +()\nend", make_fn_decl({"+"}));

  CHECK_PARSER("def -() = 0", make_fn_decl({"-"}, ast<ReturnStmt>(0_Num)));

  CHECK_PARSER("def !(a I32) I32\nend", make_fn_decl({.name = "!", .args = {{"a", "I32"_Type}}, .ret = "I32"_Type}));

  CHECK_PARSER("def [](a I32[], x Foo)\nend",
               make_fn_decl({.name = "[]", .args = {{"a", "I32"_Type & Slice}, {"x", "Foo"_Type}}}));

  CHECK_PARSER("def []=(target L, offs I32, val V) L[]\nend",
               make_fn_decl({.name = "[]=",
                             .args = {{"target", "L"_Type}, {"offs", "I32"_Type}, {"val", "V"_Type}},
                             .ret = "L"_Type & Slice}));

  CHECK_PARSER(
      "def +=(i I32 mut, j I32) I32 mut = i + j",
      make_fn_decl({.name = "+=", .args = {{"i", "I32"_Type & Mut}, {"j", "I32"_Type}}, .ret = "I32"_Type & Mut},
                   ast<ReturnStmt>(make_call("+", "i"_Var, "j"_Var))));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               make_fn_decl({.name = "baz", .args = {{"l", "Left"_Type}, {"r", "Right"_Type}}, .ret = "U32"_Type}));

  CHECK_PARSER("def foo(a I32) I32\na\nend",
               make_fn_decl({.name = "foo", .args = {{"a", "I32"_Type}}, .ret = "I32"_Type}, "a"_Var));
}

TEST_CASE("Parse primitive function declaration", "[parse][fn]") {
  CHECK_PARSER("def prim() = __primitive__(name_of_primitive)",
               make_primitive_decl("prim", {}, {}, "name_of_primitive"));
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = __primitive__(native_munge)",
               make_primitive_decl("munge", {{"a", "I32"_Type & Ptr}}, "I32"_Type & Ptr, "native_munge"));
  CHECK_PARSER("def printf(format U8 ptr) = __extern__ __varargs__",
               make_extern_decl("printf", {{"format", "U8"_Type & Ptr}}, {}, "printf", true));
}

TEST_CASE("Parse abstract function declaration", "[parse][abstract]") {
  CHECK_PARSER("def abs() = abstract", make_abstract_decl("abs", {{"", Self()}}, {}));
  CHECK_PARSER("def abs(self) = abstract", make_abstract_decl("abs", {{"self", Self()}}, {}));
  CHECK_PARSER("def abs(this Self) = abstract", make_abstract_decl("abs", {{"this", Self()}}, {}));
  CHECK_PARSER("def dup(self) Self = abstract", make_abstract_decl("dup", {{"self", Self()}}, Self()));
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = abstract",
               make_abstract_decl("munge", {{"a", "I32"_Type & Ptr}}, "I32"_Type & Ptr));
}

TEST_CASE("Parse extern linkage function declaration", "[parse][fn]") {
  CHECK_PARSER("def @extern foo()\nend", make_fn_decl({.name = "foo", .attributes = {"extern"}}));
  CHECK_PARSER("def @extern short() = 0",
               make_fn_decl({.name = "short", .attributes = {"extern"}}, ast<ReturnStmt>(0_Num)));
  CHECK_PARSER("def @extern @pure @foo short() = 0",
               make_fn_decl({.name = "short", .attributes = {"extern", "pure", "foo"}}, ast<ReturnStmt>(0_Num)));
}

TEST_CASE("Parse proxy field function declaration", "[parse][fn]") {
  CHECK_PARSER("def foo(::field)\nend",
               make_fn_decl({.name = "foo", .args = {{"field", ast<ProxyType>("field")}}},
                            ast<AssignExpr>(ast<FieldAccessExpr>(std::nullopt, "field"), "field"_Var)));
  CHECK_PARSER("def foo(a I32, ::b)\nend",
               make_fn_decl({.name = "foo", .args = {{"a", "I32"_Type}, {"b", ast<ProxyType>("b")}}},
                            ast<AssignExpr>(ast<FieldAccessExpr>(std::nullopt, "b"), "b"_Var)));
  CHECK_PARSER("def foo(::a, ::b)\nend",
               make_fn_decl({.name = "foo", .args = {{"a", ast<ProxyType>("a")}, {"b", ast<ProxyType>("b")}}},
                            ast<AssignExpr>(ast<FieldAccessExpr>(std::nullopt, "a"), "a"_Var),
                            ast<AssignExpr>(ast<FieldAccessExpr>(std::nullopt, "b"), "b"_Var)));
}

TEST_CASE("Parse incomplete def", "[parse][fn][throws]") {
  CHECK_PARSER_THROWS("def");
  CHECK_PARSER_THROWS("def foo");
  CHECK_PARSER_THROWS("def (");
  CHECK_PARSER_THROWS("def .");
  CHECK_PARSER_THROWS("def foo{");
  CHECK_PARSER_THROWS("def foo(");
  CHECK_PARSER_THROWS("def foo.");
  CHECK_PARSER_THROWS("def foo =");
  CHECK_PARSER_THROWS("def foo() =");
  CHECK_PARSER_THROWS("def foo() = __primitive__");
  CHECK_PARSER_THROWS("def foo() = __primitive__(");
  CHECK_PARSER_THROWS("def foo() = __primitive__.");
  CHECK_PARSER_THROWS("def foo() = __primitive__ foo");
  CHECK_PARSER_THROWS("def foo() = __primitive__(foo");
}

TEST_CASE("Parse struct declaration", "[parse][struct]") {
  CHECK_PARSER("struct Foo()\nend", make_struct_decl("Foo", {}, {}, {}));
  CHECK_PARSER("struct Foo\nend", make_struct_decl("Foo", {}, {}, {}));
  CHECK_PARSER("struct Foo(i I32)\nend", make_struct_decl("Foo", {{"i", "I32"_Type}}, {}, {}));
  CHECK_PARSER("struct Foo(i I32, bar Bar)\nend",
               make_struct_decl("Foo", {{"i", "I32"_Type}, {"bar", "Bar"_Type}}, {}, {}));

  CHECK_PARSER("struct Foo{T}()\nend", make_struct_decl("Foo", {}, {"T"}, {}));
  CHECK_PARSER("struct Foo{T}(t T)\nend", make_struct_decl("Foo", {{"t", "T"_Type}}, {"T"}, {}));

  CHECK_PARSER("struct Foo()\ndef method()\nend\nend",
               make_struct_decl("Foo", {}, {}, {}, make_fn_decl({.name = "method"})));

  CHECK_PARSER("struct Foo() is Bar\nend", make_struct_decl("Foo", {}, {}, "Bar"_Type));
  CHECK_PARSER("struct Foo is Bar\nend", make_struct_decl("Foo", {}, {}, "Bar"_Type));
}

TEST_CASE("Parse interface declaration", "[parse][interface]") {
  CHECK_PARSER("interface Foo()\nend", make_interface_decl("Foo", {}, {}));
  CHECK_PARSER("interface Foo\nend", make_interface_decl("Foo", {}, {}));
  CHECK_PARSER("interface Foo(i I32)\nend", make_interface_decl("Foo", {{"i", "I32"_Type}}, {}));
  CHECK_PARSER("interface Foo()\ndef method()\nend\nend",
               make_interface_decl("Foo", {}, {}, make_fn_decl({.name = "method"})));
}

TEST_CASE("Parse incomplete struct", "[parse][struct][throws]") {
  CHECK_PARSER_THROWS("struct");
  CHECK_PARSER_THROWS("struct Foo");
  CHECK_PARSER_THROWS("struct Foo(");
  CHECK_PARSER_THROWS("struct Foo()");
  CHECK_PARSER_THROWS("struct Foo() end");
  CHECK_PARSER_THROWS("struct foo");
}

TEST_CASE("Parse incomplete interface", "[parse][interface][throws]") {
  CHECK_PARSER_THROWS("interface");
  CHECK_PARSER_THROWS("interface Foo");
  CHECK_PARSER_THROWS("interface Foo(");
  CHECK_PARSER_THROWS("interface Foo()");
  CHECK_PARSER_THROWS("interface Foo() end");
  CHECK_PARSER_THROWS("interface foo");
}

#undef CHECK_PARSER
#undef CHECK_PARSER_THROWS
