#include "./test_common.hpp"
#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "diagnostic/notes.hpp"
#include "diagnostic/source_location.hpp"
#include "diagnostic/visitor/print_visitor.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <initializer_list>
#include <iterator>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <utility>

namespace {
namespace ast = yume::ast;
using std::move;
using namespace Catch::Matchers;

struct Tree {
  std::string key;
  std::string value;
  std::vector<Tree> nested;
  bool is_null = false;
  bool is_object = false;

  Tree(std::string_view key, const char* value) : key(key), value(std::string(value)) {}
  Tree(std::string_view key, Tree value) : key(key), nested{move(value)} {}
  Tree(std::string_view key, std::nullptr_t /*null*/) : key(key), is_null(true) {}
  Tree(std::string_view key, std::vector<Tree>& value) : key(key), nested{std::vector<Tree>(value)}, is_object(true) {}
  Tree(std::string_view key, Tree v1, Tree v2) : key(key), nested{move(v1), move(v2)} {}
  Tree(std::string_view key, Tree v1, Tree v2, Tree v3) : key(key), nested{move(v1), move(v2), move(v3)} {}

  void to_s(llvm::raw_ostream& os, bool within_object = false) const {
    os << key;
    if (is_null) {
      os << (within_object ? "=null" : "()");
    } else if (is_object || !nested.empty()) {
      os << (within_object ? "=" : "(");
      bool write_space = false;
      for (const auto& child : nested) {
        if (write_space)
          os << " ";
        child.to_s(os, !within_object);
        write_space = true;
      }
      os << (within_object ? "" : ")");
    } else {
      os << "=\"" << value << '"';
    }
  }
};

constexpr auto ast_comparison = [](const yume::ast::Program& a, const std::string& b) -> bool {
  std::string buffer;
  llvm::raw_string_ostream os{buffer};
  yume::diagnostic::PrintVisitor visitor(os);
  visitor.visit(a, "");
  return buffer == b;
};

auto equals_ast(std::initializer_list<Tree> s_expr) {
  auto buffer = std::string{"program("};
  auto os = llvm::raw_string_ostream{buffer};
  bool write_space = false;
  for (const auto& s : s_expr) {
    if (write_space)
      os << " ";
    os << "body=";
    s.to_s(os);
    write_space = true;
  }
  os << ")";
  return EqualsDirectMatcher<std::string, decltype(ast_comparison)>{buffer};
}

auto expected_token(const char* type, const char* payload) {
  return StartsWith(std::string{"Expected token type "} + type + " for payload " + payload + ", got ");
}
auto expected_word() { return StartsWith("Expected word, got "); }
auto expected_uword_type() { return Equals("Expected capitalized payload for simple type"); }
} // namespace

auto operator""_Var(const char* str, size_t /*size*/) -> Tree { return {"var", {"name", str}}; }
auto operator""_Num(unsigned long long val) -> Tree { return {"number", {"value", std::to_string(val).c_str()}}; }
auto operator""_String(const char* str, size_t /*size*/) -> Tree { return {"string", {"value", str}}; }
auto operator""_Char(char chr) -> Tree { return {"char", {"value", std::string(1, chr).c_str()}}; }
auto Bool(bool value) -> Tree { return {"bool", {"value", value ? "true" : "false"}}; }
auto Self() -> Tree { return {"self-type", nullptr}; }
auto SimpleType(const char* str) -> Tree { return {"simple-type", {"name", str}}; }
template <typename T = std::nullptr_t> auto GenericParam(const char* name, T type = nullptr) -> Tree {
  return {"generic-param", {"name", name}, {"type", move(type)}};
}

struct TypeBuilder {
  Tree value;

  auto slice() { return value = {"templated-type", {"base", SimpleType("Slice")}, {"type-arg", move(value)}}, *this; }
  auto operator()(auto... args) {
    std::vector<Tree> vec{{"base", move(value)}};
    (vec.emplace_back("type-arg", args), ...);
    return value = Tree{"templated-type", vec}, *this;
  }
  auto mut() { return value = {"qual-type", {"mut", move(value)}}, *this; }
  auto ptr() { return value = {"qual-type", {"ptr", move(value)}}, *this; }
  auto meta() { return value = {"qual-type", {"meta", move(value)}}, *this; }
  auto expr() { return Tree{"type-expr", {"type", move(value)}}; }

  operator Tree() const { return value; }
};
auto operator""_Type(const char* str, size_t /*size*/) { return TypeBuilder{SimpleType(str)}; }

template <yume::StringLiteral Name> struct Builder {
  std::vector<Tree> nested{};

  operator Tree() { return {static_cast<const char*>(Name.value), nested}; }

protected:
  auto add(auto* self, const char* key, auto v) {
    nested.emplace_back(key, move(v));
    return *self;
  }
};

struct FunctionTypeBuilder : Builder<"function-type"> {
  auto ret(auto v) { return add(this, "ret", move(v)); }
  auto operator()(auto... v) { return (add(this, "args", move(v)), ...); }
};
auto FunctionType(auto ret) { return FunctionTypeBuilder{}.ret(move(ret)); }

struct CallBuilder : Builder<"call"> {
  auto name(const char* v) { return add(this, "name", v); }
  auto receiver(auto v) { return add(this, "receiver", move(v)); }
  auto operator()(auto... v) { return (add(this, "args", move(v)), ...); }
};
auto Call(const char* name) { return CallBuilder{}.name(name); }

struct CtorBuilder : Builder<"constructor"> {
  auto type(Tree v) { return add(this, "type", move(v)); }
  auto operator()(auto... v) { return (add(this, "args", move(v)), ...); }
};
auto Ctor(Tree type) { return CtorBuilder{}.type(move(type)); }

struct SliceBuilder : Builder<"slice"> {
  auto type(Tree v) { return add(this, "type", move(v)); }
  auto operator()(auto... v) { return (add(this, "args", move(v)), ...); }
};
auto Slice(Tree type) { return SliceBuilder{}.type(move(type)); }

auto operator/(const char* name, Tree type) -> Tree { return {"type-name", {"name", name}, {"type", move(type)}}; }

struct FnDeclBuilder : Builder<"fn-decl"> {
  auto name(const char* v) { return add(this, "name", v); }
  auto ret(auto v) { return add(this, "ret", v); }
  auto body(auto v) { return add(this, "body", v); }
  auto primitive(const char* v) { return add(this, "primitive", v); }
  auto extern_name(const char* v) { return add(this, "extern", v); }
  auto varargs() { return add(this, "varargs", "true"); }
  auto abstract() { return add(this, "abstract", "true"); }
  auto operator()(auto... rest) { return (add(this, "arg", move(rest)), ...); }
  auto type_arg(const char* v) { return add(this, "type-arg", GenericParam(v)); }
  auto annotation(const char* v) { return add(this, "annotation", v); }
};
auto FnDecl(const char* name) { return FnDeclBuilder{}.name(name); }

struct CtorDeclBuilder : Builder<"ctor-decl"> {
  auto body(auto v) { return add(this, "body", v); }
  auto operator()(auto... rest) { return (add(this, "arg", move(rest)), ...); }
};
auto CtorDecl() { return CtorDeclBuilder{}; }

struct StructDeclBuilder : Builder<"struct-decl"> {
  auto name(const char* v) { return add(this, "name", v); }
  auto implements(auto v) { return add(this, "implements", move(v)); }
  auto operator()(auto... rest) { return (add(this, "field", move(rest)), ...); }
  auto type_arg(const char* v) { return add(this, "type-arg", GenericParam(v)); }
  auto annotation(const char* v) { return add(this, "annotation", v); }
  auto body(auto v) { return add(this, "body", move(v)); }
  auto interface() { return add(this, "interface", "true"); }
};
auto StructDecl(const char* name) { return StructDeclBuilder{}.name(name); }

auto Assign(auto target, auto value) -> Tree { return {"assign", {"target", move(target)}, {"value", move(value)}}; }
auto FieldAccess(auto target, const char* field) -> Tree {
  return {"field-access", {"base", move(target)}, {"field", field}};
}
auto BinaryLogic(const char* operation, auto lhs, auto rhs) -> Tree {
  return {"binary-logic", {"operation", operation}, {"lhs", move(lhs)}, {"rhs", move(rhs)}};
}
auto VarDecl(const char* name, auto type, auto init) -> Tree {
  return {"var-decl", {"name", name}, {"type", move(type)}, {"init", move(init)}};
}
auto Return(auto value) -> Tree { return {"return-statement", {"expr", move(value)}}; }
auto ProxyType(const char* name) -> Tree { return {"proxy-type", {"field", name}}; }

struct ParserTestFixture {
  // TODO(rymiel): Maybe make some sort of special notesholder which fails the test if a diagnostic is emitted
  yume::diagnostic::StringNotesHolder notes = {};

  auto prog(const std::string& str, yume::source_location src = yume::source_location::current())
      -> std::unique_ptr<ast::Program> {
    auto test_filename = std::string{"< parser_test"} + ":" + std::to_string(src.line()) + " >";
    auto in_stream = std::stringstream(str);
    auto tokens = yume::tokenize(in_stream, test_filename);
    auto iter = ast::TokenIterator{tokens.begin(), tokens.end()};
    return ast::Program::parse(iter, notes);
  }
};

#define CHECK_PARSER(body, ...)                                                                                        \
  CHECK_THAT(*prog(body), equals_ast({__VA_ARGS__}));                                                                  \
  if (!notes.buffer.empty()) {                                                                                         \
    FAIL_CHECK("Parser diagnostics aren't empty: " << notes.buffer);                                                   \
    notes.buffer.clear();                                                                                              \
  }
#define CHECK_PARSER_WITH_DIAGNOSTIC(body, ...) CHECK_THAT(*prog(body), equals_ast({__VA_ARGS__}));
#define CHECK_DIAGNOSTIC(checker)                                                                                      \
  CHECK_THAT(notes.buffer, checker);                                                                                   \
  notes.buffer.clear();
#define MACRO_OVERLOAD(_1, NAME, ...) NAME
#define CHECK_PARSER_FATAL(body, ...)                                                                                  \
  MACRO_OVERLOAD(__VA_ARGS__ __VA_OPT__(, ) CHECK_THROWS_WITH, CHECK_THROWS)(*prog(body) __VA_OPT__(, ) __VA_ARGS__);  \
  if (notes.buffer.empty())                                                                                            \
    FAIL_CHECK("Parser diagnostics are empty " << notes.buffer);                                                       \
  notes.buffer.clear();
#define TEST_CASE_PARSE(name, tags) TEST_CASE_METHOD(ParserTestFixture, "Parse " name, "[parse]" tags)

TEST_CASE_PARSE("literals", "") {
  CHECK_PARSER("true", Bool(true));
  CHECK_PARSER("false", Bool(false));

  CHECK_PARSER("1", 1_Num);
  CHECK_PARSER("42", 42_Num);
  CHECK_PARSER("0x0", 0_Num);
  CHECK_PARSER("0x0fc", 0xfc_Num);
  CHECK_PARSER("0x2fc", 0x2fc_Num);

  CHECK_PARSER("?a", 'a'_Char);

  CHECK_PARSER(R"("hi")", "hi"_String);
}

TEST_CASE_PARSE("operator calls", "") {
  CHECK_PARSER("1 + 2", Call("+")(1_Num, 2_Num));
  CHECK_PARSER("1 +2", Call("+")(1_Num, 2_Num));
  CHECK_PARSER("1 - 2", Call("-")(1_Num, 2_Num));
  CHECK_PARSER("1 -2", Call("-")(1_Num, 2_Num));
  CHECK_PARSER("-2", Call("-")(2_Num));
  CHECK_PARSER("!true", Call("!")(Bool(true)));
}

TEST_CASE_PARSE("operator precedence", "") {
  CHECK_PARSER("1 + 2 * 3 == 6 + 1", Call("==")(Call("+")(1_Num, Call("*")(2_Num, 3_Num)), Call("+")(6_Num, 1_Num)));
}

TEST_CASE_PARSE("assignment", "") {
  CHECK_PARSER("a = 1\nb = 2", Assign("a"_Var, 1_Num), Assign("b"_Var, 2_Num));
  CHECK_PARSER("a = b = 2", Assign("a"_Var, Assign("b"_Var, 2_Num)));
}

TEST_CASE_PARSE("plain calling", "") {
  CHECK_PARSER("a", "a"_Var);
  CHECK_PARSER("(a)", "a"_Var);
  CHECK_PARSER("a()", Call("a"));
  CHECK_PARSER("a(1, 2, 3)", Call("a")(1_Num, 2_Num, 3_Num));
  CHECK_PARSER("a(b(1), c(2, 3))", Call("a")(Call("b")(1_Num), Call("c")(2_Num, 3_Num)));
}

TEST_CASE_PARSE("direct calling", "") {
  CHECK_PARSER("a->()", Call("->")("a"_Var));
  CHECK_PARSER("(a)->()", Call("->")("a"_Var));
  CHECK_PARSER("a->(1, 2, 3)", Call("->")("a"_Var, 1_Num, 2_Num, 3_Num));
  CHECK_PARSER("a->(1)->(2)->(3)", Call("->")(Call("->")(Call("->")("a"_Var, 1_Num), 2_Num), 3_Num));
}

TEST_CASE_PARSE("member calling", "") {
  CHECK_PARSER("receiver.call", Call("call")("receiver"_Var));
  CHECK_PARSER("receiver.call()", Call("call")("receiver"_Var));
  CHECK_PARSER("receiver.call(1, 2, 3)", Call("call")("receiver"_Var, 1_Num, 2_Num, 3_Num));
}

TEST_CASE_PARSE("receiver calling", "") {
  CHECK_PARSER("Foo.call", Call("call").receiver("Foo"_Type));
  CHECK_PARSER("Foo.call()", Call("call").receiver("Foo"_Type));
  CHECK_PARSER("Foo.call(1, 2, 3)", Call("call").receiver("Foo"_Type)(1_Num, 2_Num, 3_Num));
  CHECK_PARSER("Foo[].call()", Call("call").receiver("Foo"_Type.slice()));
  CHECK_PARSER("Foo mut.call()", Call("call").receiver("Foo"_Type.mut()));
}

TEST_CASE_PARSE("setter calling", "") {
  CHECK_PARSER("receiver.foo = 0", Call("foo=")("receiver"_Var, 0_Num));
  CHECK_PARSER("r.f = r", Call("f=")("r"_Var, "r"_Var));
}

TEST_CASE_PARSE("direct field access", "") {
  CHECK_PARSER("widget::field", FieldAccess("widget"_Var, "field"));
  CHECK_PARSER("widget::field.run", Call("run")(FieldAccess("widget"_Var, "field")));
  CHECK_PARSER("widget::field = 0", Assign(FieldAccess("widget"_Var, "field"), 0_Num));

  // The following are only actually valid within constructors (for now)
  CHECK_PARSER("::field", FieldAccess(nullptr, "field"));
  CHECK_PARSER("::field.run", Call("run")(FieldAccess(nullptr, "field")));
  CHECK_PARSER("::field = 0", Assign(FieldAccess(nullptr, "field"), 0_Num));
}

TEST_CASE_PARSE("constructor calling", "") {
  CHECK_PARSER("Type()", Ctor("Type"_Type));
  CHECK_PARSER("Type(a)", Ctor("Type"_Type)("a"_Var));

  CHECK_PARSER("Type[]()", Ctor("Type"_Type.slice()));
  CHECK_PARSER("Type[](a)", Ctor("Type"_Type.slice())("a"_Var));
}

TEST_CASE_PARSE("slice literal", "") {
  CHECK_PARSER("I32:[1, 2, 3]", Slice("I32"_Type)(1_Num, 2_Num, 3_Num));
  CHECK_PARSER("I32:[]", Slice("I32"_Type));
}

TEST_CASE_PARSE("type expression", "") {
  CHECK_PARSER("T ptr", "T"_Type.ptr().expr());
  CHECK_PARSER("I32", "I32"_Type.expr());
  CHECK_PARSER("I32[]", "I32"_Type.slice().expr());
  CHECK_PARSER("0.as(I64)", Call("as")(0_Num, "I64"_Type.expr()));
}

TEST_CASE_PARSE("index operator", "") {
  CHECK_PARSER("a[b]", Call("[]")("a"_Var, "b"_Var));
  CHECK_PARSER("a[b] = c", Call("[]=")("a"_Var, "b"_Var, "c"_Var));
}

TEST_CASE_PARSE("logical operators", "") {
  CHECK_PARSER("a || b", BinaryLogic("||", "a"_Var, "b"_Var));
  CHECK_PARSER("a && b", BinaryLogic("&&", "a"_Var, "b"_Var));
  CHECK_PARSER("a || b || c", BinaryLogic("||", "a"_Var, BinaryLogic("||", "b"_Var, "c"_Var)));
  CHECK_PARSER("a && b && c", BinaryLogic("&&", "a"_Var, BinaryLogic("&&", "b"_Var, "c"_Var)));
  CHECK_PARSER("a && b || c && d",
               BinaryLogic("||", BinaryLogic("&&", "a"_Var, "b"_Var), BinaryLogic("&&", "c"_Var, "d"_Var)));
  CHECK_PARSER("a || b && c || d",
               BinaryLogic("||", "a"_Var, BinaryLogic("||", BinaryLogic("&&", "b"_Var, "c"_Var), "d"_Var)));
}

TEST_CASE_PARSE("variable declaration", "") {
  CHECK_PARSER("let a = 0", VarDecl("a", nullptr, 0_Num));
  CHECK_PARSER("let a I32 = 0", VarDecl("a", "I32"_Type, 0_Num));
}

TEST_CASE_PARSE("function declaration", "[fn]") {
  CHECK_PARSER("def foo()\nend", FnDecl("foo"));
  CHECK_PARSER("def foo(a I32)\nend", FnDecl("foo")("a" / "I32"_Type));
  CHECK_PARSER("def bar(a I32, x Foo)\nend", FnDecl("bar")(("a" / "I32"_Type), ("x" / "Foo"_Type)));
  CHECK_PARSER("def bar() U32\nend", FnDecl("bar").ret("U32"_Type));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               FnDecl("baz")(("l" / "Left"_Type), ("r" / "Right"_Type)).ret("U32"_Type));

  CHECK_PARSER("def foo(a I32) I32\na\nend", FnDecl("foo")("a" / "I32"_Type).ret("I32"_Type).body("a"_Var));
}

TEST_CASE_PARSE("function declaration with body", "[fn]") {
  CHECK_PARSER("def foo()\nbar()\nend", FnDecl("foo").body(Call("bar")));
  CHECK_PARSER("def foo() I32\nreturn 0\nend", FnDecl("foo").ret("I32"_Type).body(Return(0_Num)));
  CHECK_PARSER("def bar() Nil\nreturn\nend", FnDecl("bar").ret("Nil"_Type).body(Return(nullptr)));
}

TEST_CASE_PARSE("ctor declaration", "[fn]") {
  CHECK_PARSER("def :new()\nend", CtorDecl());
  CHECK_PARSER("def :new() end", CtorDecl());
  CHECK_PARSER("def :new(::field)\nend",
               CtorDecl()("field" / ProxyType("field")).body(Assign(FieldAccess(nullptr, "field"), "field"_Var)));
  CHECK_PARSER("def :new(field I32)\n::field = field\nend",
               CtorDecl()("field" / "I32"_Type).body(Assign(FieldAccess(nullptr, "field"), "field"_Var)));
  CHECK_PARSER("def :new(foo I32, bar U32) end", CtorDecl()(("foo" / "I32"_Type), ("bar" / "U32"_Type)));
}

TEST_CASE_PARSE("function declaration with function type parameter", "[fn]") {
  CHECK_PARSER("def foo(fn (->)) = fn->()",
               FnDecl("foo")("fn" / FunctionType(nullptr)).body(Return(Call("->")("fn"_Var))));
  CHECK_PARSER("def foo(fn (I32 ->)) = fn->(0)",
               FnDecl("foo")("fn" / FunctionType(nullptr)("I32"_Type)).body(Return(Call("->")("fn"_Var, 0_Num))));
  CHECK_PARSER("def foo(fn (-> I32)) I32 = fn->()",
               FnDecl("foo")("fn" / FunctionType("I32"_Type)).ret("I32"_Type).body(Return(Call("->")("fn"_Var))));
  CHECK_PARSER("def foo(fn (I32 -> I32)) I32 = fn->(0)", FnDecl("foo")("fn" / FunctionType("I32"_Type)("I32"_Type))
                                                             .ret("I32"_Type)
                                                             .body(Return(Call("->")("fn"_Var, 0_Num))));
  CHECK_PARSER("def foo(fn (I32, I32 -> I32)) I32 = fn->(0, 1)",
               FnDecl("foo")("fn" / FunctionType("I32"_Type)("I32"_Type, "I32"_Type))
                   .ret("I32"_Type)
                   .body(Return(Call("->")("fn"_Var, 0_Num, 1_Num))));
  CHECK_PARSER("def foo(fn ((I32 ->) -> (-> I32))) I32 = fn->(magic)->()",
               FnDecl("foo")("fn" / FunctionType(FunctionType("I32"_Type))(FunctionType(nullptr)("I32"_Type)))
                   .ret("I32"_Type)
                   .body(Return(Call("->")(Call("->")("fn"_Var, "magic"_Var)))));
}

TEST_CASE_PARSE("function declaration with metatype parameter", "[fn]") {
  CHECK_PARSER("def foo{T type}(type T type) T = T()",
               FnDecl("foo")("type" / "T"_Type.meta()).type_arg("T").ret("T"_Type).body(Return(Ctor("T"_Type))));
}

TEST_CASE_PARSE("function declaration with generic parameter", "[fn]") {
  CHECK_PARSER("def first{T type}(arr Array{T}) T = arr[0]", FnDecl("first")("arr" / "Array"_Type("T"_Type))
                                                                 .type_arg("T")
                                                                 .ret("T"_Type)
                                                                 .body(Return(Call("[]")("arr"_Var, 0_Num))));
  CHECK_PARSER("def first{T type}(arr Array{T mut}) T mut = arr[0]",
               FnDecl("first")("arr" / "Array"_Type("T"_Type.mut()))
                   .type_arg("T")
                   .ret("T"_Type.mut())
                   .body(Return(Call("[]")("arr"_Var, 0_Num))));
  CHECK_PARSER(
      "def first{T type, U type}(arr Container{T, Array{U}, (T -> U)})\nend",
      FnDecl("first")("arr" / "Container"_Type("T"_Type, "Array"_Type("U"_Type), FunctionType("U"_Type)("T"_Type)))
          .type_arg("T")
          .type_arg("U"));
}

TEST_CASE_PARSE("short function declaration", "[fn]") {
  CHECK_PARSER("def short() = 0", FnDecl("short").body(Return(0_Num)));
  CHECK_PARSER("def short(a I32) = a", FnDecl("short")("a" / "I32"_Type).body(Return("a"_Var)));

  CHECK_PARSER("def short(a I32) I32 = a", FnDecl("short")("a" / "I32"_Type).ret("I32"_Type).body(Return("a"_Var)));

  CHECK_PARSER("def short(a I32, b I32) I32 = a + b", FnDecl("short")(("a" / "I32"_Type), ("b" / "I32"_Type))
                                                          .ret("I32"_Type)
                                                          .body(Return(Call("+")("a"_Var, "b"_Var))));
}

TEST_CASE_PARSE("templated function declaration", "[fn]") {
  CHECK_PARSER("def templated{T type}()\nend", FnDecl("templated").type_arg("T"));

  CHECK_PARSER("def templated{T type}(t T) T\nend", FnDecl("templated")("t" / "T"_Type).type_arg("T").ret("T"_Type));

  CHECK_PARSER("def templated{T type}(t T) T = t",
               FnDecl("templated")("t" / "T"_Type).type_arg("T").ret("T"_Type).body(Return("t"_Var)));

  CHECK_PARSER("def templated{T type,U type,V type,X type,Y type,Z type}(t T, u U, v V) X = Y() + Z()",
               FnDecl("templated")(("t" / "T"_Type), ("u" / "U"_Type), ("v" / "V"_Type))
                   .type_arg("T")
                   .type_arg("U")
                   .type_arg("V")
                   .type_arg("X")
                   .type_arg("Y")
                   .type_arg("Z")
                   .ret("X"_Type)
                   .body(Return(Call("+")(Ctor("Y"_Type), Ctor("Z"_Type)))));

  CHECK_PARSER("def templated{T type}(slice T[], pointer T ptr, mutable T mut, mix T ptr[] mut)\nend",
               FnDecl("templated")(("slice" / "T"_Type.slice()), ("pointer" / "T"_Type.ptr()),
                                   ("mutable" / "T"_Type.mut()), ("mix" / "T"_Type.ptr().slice().mut()))
                   .type_arg("T"));

  // TODO(rymiel): non-type generic parameters

  CHECK_PARSER_WITH_DIAGNOSTIC("def templated{T}()\nend", FnDecl("templated").type_arg("T"));
  CHECK_DIAGNOSTIC(StartsWith("warn: ") && EndsWith("Type parameter with no specifier will be deprecated\n"));
}

TEST_CASE_PARSE("operator function declaration", "[fn]") {
  CHECK_PARSER("def +()\nend", FnDecl("+"));

  CHECK_PARSER("def -() = 0", FnDecl("-").body(Return(0_Num)));

  CHECK_PARSER("def !(a I32) I32\nend", FnDecl("!")("a" / "I32"_Type).ret("I32"_Type));

  CHECK_PARSER("def [](a I32[], x Foo)\nend", FnDecl("[]")(("a" / "I32"_Type.slice()), ("x" / "Foo"_Type)));

  CHECK_PARSER("def []=(target L, offs I32, val V) L[]\nend",
               FnDecl("[]=")(("target" / "L"_Type), ("offs" / "I32"_Type), ("val" / "V"_Type)).ret("L"_Type.slice()));

  CHECK_PARSER("def +=(i I32 mut, j I32) I32 mut = i + j", FnDecl("+=")(("i" / "I32"_Type.mut()), ("j" / "I32"_Type))
                                                               .ret("I32"_Type.mut())
                                                               .body(Return(Call("+")("i"_Var, "j"_Var))));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               FnDecl("baz")(("l" / "Left"_Type), ("r" / "Right"_Type)).ret("U32"_Type));

  CHECK_PARSER("def foo(a I32) I32\na\nend", FnDecl("foo")("a" / "I32"_Type).ret("I32"_Type).body("a"_Var));
}

TEST_CASE_PARSE("primitive function declaration", "[fn]") {
  CHECK_PARSER("def prim() = __primitive__(name_of_primitive)", FnDecl("prim").primitive("name_of_primitive"));
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = __primitive__(native_munge)",
               FnDecl("munge")("a" / "I32"_Type.ptr()).ret("I32"_Type.ptr()).primitive("native_munge"));
  CHECK_PARSER("def printf(format U8 ptr) = __extern__ __varargs__",
               FnDecl("printf")("format" / "U8"_Type.ptr()).extern_name("printf").varargs());
}

TEST_CASE_PARSE("abstract function declaration", "[abstract]") {
  CHECK_PARSER("def abs() = abstract", FnDecl("abs")("" / Self()).abstract());
  CHECK_PARSER("def abs(self) = abstract", FnDecl("abs")("self" / Self()).abstract());
  CHECK_PARSER("def abs(this Self) = abstract", FnDecl("abs")("this" / Self()).abstract());
  CHECK_PARSER("def dup(self) Self = abstract", FnDecl("dup")("self" / Self()).ret(Self()).abstract());
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = abstract",
               FnDecl("munge")("a" / "I32"_Type.ptr()).ret("I32"_Type.ptr()).abstract());
}

TEST_CASE_PARSE("extern linkage function declaration", "[fn]") {
  CHECK_PARSER("def @extern foo()\nend", FnDecl("foo").annotation("extern"));
  CHECK_PARSER("def @extern short() = 0", FnDecl("short").annotation("extern").body(Return(0_Num)));
  CHECK_PARSER("def @extern @pure @foo short() = 0",
               FnDecl("short").annotation("extern").annotation("foo").annotation("pure").body(Return(0_Num)));
}

TEST_CASE_PARSE("proxy field function declaration", "[fn]") {
  CHECK_PARSER("def foo(::field)\nend",
               FnDecl("foo")("field" / ProxyType("field")).body(Assign(FieldAccess(nullptr, "field"), "field"_Var)));
  CHECK_PARSER(
      "def foo(a I32, ::b)\nend",
      FnDecl("foo")(("a" / "I32"_Type), ("b" / ProxyType("b"))).body(Assign(FieldAccess(nullptr, "b"), "b"_Var)));
  CHECK_PARSER("def foo(::a, ::b)\nend", FnDecl("foo")(("a" / ProxyType("a")), ("b" / ProxyType("b")))
                                             .body(Assign(FieldAccess(nullptr, "a"), "a"_Var))
                                             .body(Assign(FieldAccess(nullptr, "b"), "b"_Var)));
}

TEST_CASE_PARSE("incomplete def", "[fn][throws]") {
  CHECK_PARSER_FATAL("def");
  CHECK_PARSER_FATAL("def foo", expected_token("Symbol", "("));
  CHECK_PARSER_FATAL("def (", expected_word());
  CHECK_PARSER_FATAL("def .");
  CHECK_PARSER_FATAL("def foo{");
  CHECK_PARSER_FATAL("def foo{T");
  CHECK_PARSER_FATAL("def foo{T type");
  CHECK_PARSER_FATAL("def foo{t");
  CHECK_PARSER_FATAL("def foo{t I32");
  CHECK_PARSER_FATAL("def foo(", expected_word());
  CHECK_PARSER_FATAL("def foo(a", expected_word());
  CHECK_PARSER_FATAL("def foo(a I32");
  CHECK_PARSER_FATAL("def foo.");
  CHECK_PARSER_FATAL("def foo =");
  CHECK_PARSER_FATAL("def foo() =");
  CHECK_PARSER_FATAL("def foo() = __primitive__");
  CHECK_PARSER_FATAL("def foo() = __primitive__(");
  CHECK_PARSER_FATAL("def foo() = __primitive__.");
  CHECK_PARSER_FATAL("def foo() = __primitive__ foo");
  CHECK_PARSER_FATAL("def foo() = __primitive__(foo", expected_token("Symbol", ")"));
}

TEST_CASE_PARSE("invalid type", "[throws]") {
  CHECK_PARSER_FATAL("let x I32[");
  CHECK_PARSER_FATAL("let x y");
  CHECK_PARSER_FATAL("let x y z");
  CHECK_PARSER_FATAL("let x y = z");
  CHECK_PARSER_FATAL("def foo(a b)", expected_uword_type());
  CHECK_PARSER_FATAL("def foo(a self)", expected_uword_type());
  CHECK_PARSER_FATAL("def foo(a I32) b");
  CHECK_PARSER_FATAL("def foo(a I32) self");
  CHECK_PARSER_FATAL("def foo{t u}()", expected_uword_type());
  CHECK_PARSER_FATAL("def foo{T u}()");
  CHECK_PARSER_FATAL("def foo{T U}()");
  CHECK_PARSER_FATAL("struct Foo(a b)", expected_uword_type());
  CHECK_PARSER_FATAL("struct Foo(a self)", expected_uword_type());
  CHECK_PARSER_FATAL("struct Foo(self)");
  CHECK_PARSER_FATAL("def :new() I32");
}

TEST_CASE_PARSE("struct declaration", "[struct]") {
  CHECK_PARSER("struct Foo()\nend", StructDecl("Foo"));
  CHECK_PARSER("struct Foo\nend", StructDecl("Foo"));
  CHECK_PARSER("struct Foo(i I32)\nend", StructDecl("Foo")("i" / "I32"_Type));
  CHECK_PARSER("struct Foo(i I32, bar Bar)\nend", StructDecl("Foo")(("i" / "I32"_Type), ("bar" / "Bar"_Type)));

  CHECK_PARSER("struct Foo{T type}()\nend", StructDecl("Foo").type_arg("T"));
  CHECK_PARSER("struct Foo{T type}(t T)\nend", StructDecl("Foo")("t" / "T"_Type).type_arg("T"));

  CHECK_PARSER("struct Foo()\ndef method()\nend\nend", StructDecl("Foo").body(FnDecl("method")));

  CHECK_PARSER("struct Foo() is Bar\nend", StructDecl("Foo").implements("Bar"_Type));
  CHECK_PARSER("struct Foo is Bar\nend", StructDecl("Foo").implements("Bar"_Type));
}

TEST_CASE_PARSE("interface declaration", "[interface]") {
  CHECK_PARSER("interface Foo()\nend", StructDecl("Foo").interface());
  CHECK_PARSER("interface Foo\nend", StructDecl("Foo").interface());
  CHECK_PARSER("interface Foo(i I32)\nend", StructDecl("Foo")("i" / "I32"_Type).interface());
  CHECK_PARSER("interface Foo()\ndef method()\nend\nend", StructDecl("Foo").body(FnDecl("method")).interface());
}

TEST_CASE_PARSE("incomplete struct", "[throws]") {
  CHECK_PARSER_FATAL("struct", expected_word());
  CHECK_PARSER_FATAL("struct Foo");
  CHECK_PARSER_FATAL("struct Foo(");
  CHECK_PARSER_FATAL("struct Foo()");
  CHECK_PARSER_FATAL("struct Foo() end");
  CHECK_PARSER_FATAL("struct foo", Equals("Expected capitalized name for struct decl"));
}

TEST_CASE_PARSE("incomplete interface", "[interface][throws]") {
  CHECK_PARSER_FATAL("interface");
  CHECK_PARSER_FATAL("interface Foo");
  CHECK_PARSER_FATAL("interface Foo(");
  CHECK_PARSER_FATAL("interface Foo()");
  CHECK_PARSER_FATAL("interface Foo() end");
  CHECK_PARSER_FATAL("interface foo", Equals("Expected capitalized name for struct decl"));
}
