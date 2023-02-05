#include "./test_common.hpp"
#include "ast/ast.hpp"
#include "ast/parser.hpp"
#include "atom.hpp"
#include "diagnostic/notes.hpp"
#include "diagnostic/source_location.hpp"
#include "diagnostic/visitor/print_visitor.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <iterator>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <utility>

namespace {
namespace ast = yume::ast;

auto prog(const std::string& str, yume::source_location src = yume::source_location::current())
    -> std::unique_ptr<ast::Program> {
  auto test_filename = std::string{"< parser_test"} + ":" + std::to_string(src.line()) + " >";
  auto in_stream = std::stringstream(str);
  auto tokens = yume::tokenize(in_stream, test_filename);
  auto iter = ast::TokenIterator{tokens.begin(), tokens.end()};
  auto notes = yume::diagnostic::NotesHolder{};
  // TODO(rymiel): Maybe make some sort of special notesholder which fails the test if a diagnostic is emitted
  return ast::Program::parse(iter, notes);
}

constexpr auto ast_comparison = [](const yume::ast::Program& a, const std::string& b) -> bool {
  std::string buffer;
  llvm::raw_string_ostream os{buffer};
  yume::diagnostic::PrintVisitor visitor(os);
  visitor.visit(a, "");
  return buffer == b;
};

struct Tree {
  std::string key;
  std::string value;
  std::vector<Tree> nested;
  bool is_null = false;

  Tree(std::string key, const char* value) : key(std::move(key)), value(std::string(value)) {}
  Tree(std::string key, Tree value) : key(std::move(key)), nested{std::move(value)} {}
  Tree(std::string key, std::nullptr_t /*null*/) : key(std::move(key)), is_null(true) {}
  Tree(std::string key, std::initializer_list<Tree> value) : key(std::move(key)), nested{value} {}
  Tree(std::string key, std::vector<Tree> value, bool /*tag*/) : key(std::move(key)), nested{std::move(value)} {}

  void to_s(llvm::raw_ostream& os, bool within_object = false) const {
    os << key;
    if (is_null) {
      os << (within_object ? "=null" : "()");
    } else if (!nested.empty()) {
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
} // namespace

auto operator""_Var(const char* str, size_t /*size*/) -> Tree { return {"var", {"name", str}}; }
auto operator""_Num(unsigned long long val) -> Tree { return {"number", {"value", std::to_string(val).c_str()}}; }
auto operator""_String(const char* str, size_t /*size*/) -> Tree { return {"string", {"value", str}}; }
auto operator""_Char(char chr) -> Tree { return {"char", {"value", std::string(1, chr).c_str()}}; }
// NOLINTNEXTLINE(readability-identifier-naming)
auto Bool(bool value) -> Tree { return {"bool", {"value", value ? "true" : "false"}}; }
// NOLINTNEXTLINE(readability-identifier-naming)
auto Self() -> Tree { return {"self-type", nullptr}; }

struct TypeBuilder {
  Tree value;

  auto slice() {
    return value = {"templated-type", {{"base", {"simple-type", {"name", "Slice"}}}, {"type-arg", std::move(value)}}},
           *this;
  }
  auto mut() { return value = {"qual-type", {{"mut", std::move(value)}}}, *this; }
  auto ptr() { return value = {"qual-type", {{"ptr", std::move(value)}}}, *this; }
  auto expr() { return Tree{"type-expr", {"type", std::move(value)}}; }

  operator Tree() const { return value; }
};
auto operator""_Type(const char* str, size_t /*size*/) { return TypeBuilder{{"simple-type", {"name", str}}}; }

struct CallBuilder {
  std::vector<Tree> nested{};

  auto name(const char* v) { return nested.emplace_back("name", v), *this; }
  auto receiver(auto v) { return nested.emplace_back("receiver", std::move(v)), *this; }
  auto operator()(auto... v) { return (nested.emplace_back("args", std::move(v)), ...), *this; }

  operator Tree() { return {"call", std::move(nested), true}; }
};
// NOLINTNEXTLINE(readability-identifier-naming)
auto Call(const char* name) { return CallBuilder{}.name(name); }

struct CtorBuilder {
  std::vector<Tree> nested{};

  auto type(Tree v) { return nested.emplace_back("type", std::move(v)), *this; }
  auto operator()(auto... v) { return (nested.emplace_back("args", std::move(v)), ...), *this; }

  operator Tree() { return {"constructor", std::move(nested), true}; }
};
// NOLINTNEXTLINE(readability-identifier-naming)
auto Ctor(Tree type) { return CtorBuilder{}.type(std::move(type)); }

struct SliceBuilder {
  std::vector<Tree> nested{};

  auto type(Tree v) { return nested.emplace_back("type", std::move(v)), *this; }
  auto operator()(auto... v) { return (nested.emplace_back("args", std::move(v)), ...), *this; }

  operator Tree() { return {"slice", std::move(nested), true}; }
};
// NOLINTNEXTLINE(readability-identifier-naming)
auto Slice(Tree type) { return SliceBuilder{}.type(std::move(type)); }

struct TName {
  const char* name{};
  Tree type;

  operator Tree() { return {"type-name", {{"name", name}, {"type", std::move(type)}}}; }
};

auto operator/(const char* name, Tree type) -> TName { return {name, std::move(type)}; }

struct FnDeclBuilder {
  std::vector<Tree> nested{};

  auto name(const char* v) { return nested.emplace_back("name", v), *this; }
  auto ret(auto v) { return nested.emplace_back("ret", std::move(v)), *this; }
  auto body(auto v) { return nested.emplace_back("body", std::move(v)), *this; }
  auto primitive(const char* v) { return nested.emplace_back("primitive", v), *this; }
  auto extern_name(const char* v) { return nested.emplace_back("extern", v), *this; }
  auto varargs() { return nested.emplace_back("varargs", "true"), *this; }
  auto abstract() { return nested.emplace_back("abstract", "true"), *this; }
  auto operator()(auto... rest) { return (nested.emplace_back("arg", std::move(rest)), ...), *this; }
  auto type_arg(const char* v) {
    return nested.push_back({"type-arg", {"generic-param", {{"name", v}, {"type", nullptr}}}}), *this;
  }
  auto annotation(const char* v) { return nested.emplace_back("annotation", v), *this; }

  operator Tree() { return {"fn-decl", std::move(nested), true}; }
};
// NOLINTNEXTLINE(readability-identifier-naming)
auto FnDecl(const char* name) { return FnDeclBuilder{}.name(name); }

struct StructDeclBuilder {
  std::vector<Tree> nested{};

  auto name(const char* v) { return nested.emplace_back("name", v), *this; }
  auto implements(auto v) { return nested.emplace_back("implements", std::move(v)), *this; }
  auto operator()(auto... rest) { return (nested.emplace_back("field", std::move(rest)), ...), *this; }
  auto type_arg(const char* v) {
    return nested.push_back({"type-arg", {"generic-param", {{"name", v}, {"type", nullptr}}}}), *this;
  }
  auto annotation(const char* v) { return nested.emplace_back("annotation", v), *this; }
  auto body(auto v) { return nested.emplace_back("body", std::move(v)), *this; }
  auto interface() { return nested.emplace_back("interface", "true"), *this; }

  operator Tree() { return {"struct-decl", std::move(nested), true}; }
};
// NOLINTNEXTLINE(readability-identifier-naming)
auto StructDecl(const char* name) { return StructDeclBuilder{}.name(name); }

// NOLINTNEXTLINE(readability-identifier-naming)
auto Assign(auto target, auto value) -> Tree {
  return {"assign", {{"target", std::move(target)}, {"value", std::move(value)}}};
}
// NOLINTNEXTLINE(readability-identifier-naming)
auto FieldAccess(auto target, const char* field) -> Tree {
  return {"field-access", {{"base", std::move(target)}, {"field", field}}};
}
// NOLINTNEXTLINE(readability-identifier-naming)
auto BinaryLogic(const char* operation, auto lhs, auto rhs) -> Tree {
  return {"binary-logic", {{"operation", operation}, {"lhs", std::move(lhs)}, {"rhs", std::move(rhs)}}};
}
// NOLINTNEXTLINE(readability-identifier-naming)
auto VarDecl(const char* name, auto type, auto init) -> Tree {
  return {"var-decl", {{"name", name}, {"type", std::move(type)}, {"init", std::move(init)}}};
}
// NOLINTNEXTLINE(readability-identifier-naming)
auto Return(auto value) -> Tree { return {"return-statement", {{"expr", std::move(value)}}}; }
// NOLINTNEXTLINE(readability-identifier-naming)
auto ProxyType(const char* name) -> Tree { return {"proxy-type", {{"field", name}}}; }

#define CHECK_PARSER_OLD(body, ...) CHECK_THAT(*prog(body), equals_ast({}))
#define CHECK_PARSER_THROWS(body) CHECK_THROWS(*prog(body))

#define CHECK_PARSER(body, ...) CHECK_THAT(*prog(body), equals_ast({__VA_ARGS__}))

// using namespace yume::ast;
using enum yume::Qualifier;
using yume::operator""_a;

TEST_CASE("Parse literals", "[parse]") {
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

TEST_CASE("Parse operator calls", "[parse]") {
  CHECK_PARSER("1 + 2", Call("+")(1_Num, 2_Num));
  CHECK_PARSER("1 +2", Call("+")(1_Num, 2_Num));
  CHECK_PARSER("1 - 2", Call("-")(1_Num, 2_Num));
  CHECK_PARSER("1 -2", Call("-")(1_Num, 2_Num));
  CHECK_PARSER("-2", Call("-")(2_Num));
  CHECK_PARSER("!true", Call("!")(Bool(true)));
}

TEST_CASE("Parse operator precedence", "[parse]") {
  CHECK_PARSER("1 + 2 * 3 == 6 + 1", Call("==")(Call("+")(1_Num, Call("*")(2_Num, 3_Num)), Call("+")(6_Num, 1_Num)));
}

TEST_CASE("Parse assignment", "[parse]") {
  CHECK_PARSER("a = 1\nb = 2", Assign("a"_Var, 1_Num), Assign("b"_Var, 2_Num));
  CHECK_PARSER("a = b = 2", Assign("a"_Var, Assign("b"_Var, 2_Num)));
}

TEST_CASE("Parse direct calling", "[parse]") {
  CHECK_PARSER("a", "a"_Var);
  CHECK_PARSER("(a)", "a"_Var);
  CHECK_PARSER("a()", Call("a"));
  CHECK_PARSER("a(1, 2, 3)", Call("a")(1_Num, 2_Num, 3_Num));
  CHECK_PARSER("a(b(1), c(2, 3))", Call("a")(Call("b")(1_Num), Call("c")(2_Num, 3_Num)));
}

TEST_CASE("Parse member calling", "[parse]") {
  CHECK_PARSER("receiver.call", Call("call")("receiver"_Var));
  CHECK_PARSER("receiver.call()", Call("call")("receiver"_Var));
  CHECK_PARSER("receiver.call(1, 2, 3)", Call("call")("receiver"_Var, 1_Num, 2_Num, 3_Num));
}

TEST_CASE("Parse receiver calling", "[parse]") {
  CHECK_PARSER("Foo.call", Call("call").receiver("Foo"_Type));
  CHECK_PARSER("Foo.call()", Call("call").receiver("Foo"_Type));
  CHECK_PARSER("Foo.call(1, 2, 3)", Call("call").receiver("Foo"_Type)(1_Num, 2_Num, 3_Num));
  CHECK_PARSER("Foo[].call()", Call("call").receiver("Foo"_Type.slice()));
  CHECK_PARSER("Foo mut.call()", Call("call").receiver("Foo"_Type.mut()));
}

TEST_CASE("Parse setter calling", "[parse]") {
  CHECK_PARSER("receiver.foo = 0", Call("foo=")("receiver"_Var, 0_Num));
  CHECK_PARSER("r.f = r", Call("f=")("r"_Var, "r"_Var));
}

TEST_CASE("Parse direct field access", "[parse]") {
  CHECK_PARSER("widget::field", FieldAccess("widget"_Var, "field"));
  CHECK_PARSER("widget::field.run", Call("run")(FieldAccess("widget"_Var, "field")));

  CHECK_PARSER("widget::field = 0", Assign(FieldAccess("widget"_Var, "field"), 0_Num));
}

TEST_CASE("Parse constructor calling", "[parse]") {
  CHECK_PARSER("Type()", Ctor("Type"_Type));
  CHECK_PARSER("Type(a)", Ctor("Type"_Type)("a"_Var));

  CHECK_PARSER("Type[]()", Ctor("Type"_Type.slice()));
  CHECK_PARSER("Type[](a)", Ctor("Type"_Type.slice())("a"_Var));
}

TEST_CASE("Parse slice literal", "[parse]") {
  CHECK_PARSER("I32:[1, 2, 3]", Slice("I32"_Type)(1_Num, 2_Num, 3_Num));
  CHECK_PARSER("I32:[]", Slice("I32"_Type));
}

TEST_CASE("Parse type expression", "[parse]") {
  CHECK_PARSER("T ptr", "T"_Type.ptr().expr());
  CHECK_PARSER("I32", "I32"_Type.expr());
  CHECK_PARSER("I32[]", "I32"_Type.slice().expr());
  CHECK_PARSER("0.as(I64)", Call("as")(0_Num, "I64"_Type.expr()));
}

TEST_CASE("Parse index operator", "[parse]") {
  CHECK_PARSER("a[b]", Call("[]")("a"_Var, "b"_Var));
  CHECK_PARSER("a[b] = c", Call("[]=")("a"_Var, "b"_Var, "c"_Var));
}

TEST_CASE("Parse logical operators", "[parse]") {
  CHECK_PARSER("a || b", BinaryLogic("||", "a"_Var, "b"_Var));
  CHECK_PARSER("a && b", BinaryLogic("&&", "a"_Var, "b"_Var));
  CHECK_PARSER("a || b || c", BinaryLogic("||", "a"_Var, BinaryLogic("||", "b"_Var, "c"_Var)));
  CHECK_PARSER("a && b && c", BinaryLogic("&&", "a"_Var, BinaryLogic("&&", "b"_Var, "c"_Var)));
  CHECK_PARSER("a && b || c && d",
               BinaryLogic("||", BinaryLogic("&&", "a"_Var, "b"_Var), BinaryLogic("&&", "c"_Var, "d"_Var)));
  CHECK_PARSER("a || b && c || d",
               BinaryLogic("||", "a"_Var, BinaryLogic("||", BinaryLogic("&&", "b"_Var, "c"_Var), "d"_Var)));
}

TEST_CASE("Parse variable declaration", "[parse]") {
  CHECK_PARSER("let a = 0", VarDecl("a", nullptr, 0_Num));
  CHECK_PARSER("let a I32 = 0", VarDecl("a", "I32"_Type, 0_Num));
}

TEST_CASE("Parse function declaration", "[parse][fn]") {
  CHECK_PARSER("def foo()\nend", FnDecl("foo"));
  CHECK_PARSER("def foo(a I32)\nend", FnDecl("foo")("a" / "I32"_Type));
  CHECK_PARSER("def bar(a I32, x Foo)\nend", FnDecl("bar")(("a" / "I32"_Type), ("x" / "Foo"_Type)));
  CHECK_PARSER("def bar() U32\nend", FnDecl("bar").ret("U32"_Type));

  CHECK_PARSER("def baz(l Left, r Right) U32\nend",
               FnDecl("baz")(("l" / "Left"_Type), ("r" / "Right"_Type)).ret("U32"_Type));

  CHECK_PARSER("def foo(a I32) I32\na\nend", FnDecl("foo")("a" / "I32"_Type).ret("I32"_Type).body("a"_Var));
}

TEST_CASE("Parse short function declaration", "[parse][fn]") {
  CHECK_PARSER("def short() = 0", FnDecl("short").body(Return(0_Num)));
  CHECK_PARSER("def short(a I32) = a", FnDecl("short")("a" / "I32"_Type).body(Return("a"_Var)));

  CHECK_PARSER("def short(a I32) I32 = a", FnDecl("short")("a" / "I32"_Type).ret("I32"_Type).body(Return("a"_Var)));

  CHECK_PARSER("def short(a I32, b I32) I32 = a + b", FnDecl("short")(("a" / "I32"_Type), ("b" / "I32"_Type))
                                                          .ret("I32"_Type)
                                                          .body(Return(Call("+")("a"_Var, "b"_Var))));
}

TEST_CASE("Parse templated function declaration", "[parse][fn]") {
  CHECK_PARSER("def templated{T}()\nend", FnDecl("templated").type_arg("T"));

  CHECK_PARSER("def templated{T}(t T) T\nend", FnDecl("templated")("t" / "T"_Type).type_arg("T").ret("T"_Type));

  CHECK_PARSER("def templated{T}(t T) T = t",
               FnDecl("templated")("t" / "T"_Type).type_arg("T").ret("T"_Type).body(Return("t"_Var)));

  CHECK_PARSER("def templated{T,U,V,X,Y,Z}(t T, u U, v V) X = Y() + Z()",
               FnDecl("templated")(("t" / "T"_Type), ("u" / "U"_Type), ("v" / "V"_Type))
                   .type_arg("T")
                   .type_arg("U")
                   .type_arg("V")
                   .type_arg("X")
                   .type_arg("Y")
                   .type_arg("Z")
                   .ret("X"_Type)
                   .body(Return(Call("+")(Ctor("Y"_Type), Ctor("Z"_Type)))));

  CHECK_PARSER("def templated{T}(slice T[], pointer T ptr, mutable T mut, mix T ptr[] mut)\nend",
               FnDecl("templated")(("slice" / "T"_Type.slice()), ("pointer" / "T"_Type.ptr()),
                                   ("mutable" / "T"_Type.mut()), ("mix" / "T"_Type.ptr().slice().mut()))
                   .type_arg("T"));
}

TEST_CASE("Parse operator function declaration", "[parse][fn]") {
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

TEST_CASE("Parse primitive function declaration", "[parse][fn]") {
  CHECK_PARSER("def prim() = __primitive__(name_of_primitive)", FnDecl("prim").primitive("name_of_primitive"));
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = __primitive__(native_munge)",
               FnDecl("munge")("a" / "I32"_Type.ptr()).ret("I32"_Type.ptr()).primitive("native_munge"));
  CHECK_PARSER("def printf(format U8 ptr) = __extern__ __varargs__",
               FnDecl("printf")("format" / "U8"_Type.ptr()).extern_name("printf").varargs());
}

TEST_CASE("Parse abstract function declaration", "[parse][abstract]") {
  CHECK_PARSER("def abs() = abstract", FnDecl("abs")("" / Self()).abstract());
  CHECK_PARSER("def abs(self) = abstract", FnDecl("abs")("self" / Self()).abstract());
  CHECK_PARSER("def abs(this Self) = abstract", FnDecl("abs")("this" / Self()).abstract());
  CHECK_PARSER("def dup(self) Self = abstract", FnDecl("dup")("self" / Self()).ret(Self()).abstract());
  CHECK_PARSER("def munge(a I32 ptr) I32 ptr = abstract",
               FnDecl("munge")("a" / "I32"_Type.ptr()).ret("I32"_Type.ptr()).abstract());
}

TEST_CASE("Parse extern linkage function declaration", "[parse][fn]") {
  CHECK_PARSER("def @extern foo()\nend", FnDecl("foo").annotation("extern"));
  CHECK_PARSER("def @extern short() = 0", FnDecl("short").annotation("extern").body(Return(0_Num)));
  CHECK_PARSER("def @extern @pure @foo short() = 0",
               FnDecl("short").annotation("extern").annotation("foo").annotation("pure").body(Return(0_Num)));
}

TEST_CASE("Parse proxy field function declaration", "[parse][fn]") {
  CHECK_PARSER("def foo(::field)\nend",
               FnDecl("foo")("field" / ProxyType("field")).body(Assign(FieldAccess(nullptr, "field"), "field"_Var)));
  CHECK_PARSER(
      "def foo(a I32, ::b)\nend",
      FnDecl("foo")(("a" / "I32"_Type), ("b" / ProxyType("b"))).body(Assign(FieldAccess(nullptr, "b"), "b"_Var)));
  CHECK_PARSER("def foo(::a, ::b)\nend", FnDecl("foo")(("a" / ProxyType("a")), ("b" / ProxyType("b")))
                                             .body(Assign(FieldAccess(nullptr, "a"), "a"_Var))
                                             .body(Assign(FieldAccess(nullptr, "b"), "b"_Var)));
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
  CHECK_PARSER("struct Foo()\nend", StructDecl("Foo"));
  CHECK_PARSER("struct Foo\nend", StructDecl("Foo"));
  CHECK_PARSER("struct Foo(i I32)\nend", StructDecl("Foo")("i" / "I32"_Type));
  CHECK_PARSER("struct Foo(i I32, bar Bar)\nend", StructDecl("Foo")(("i" / "I32"_Type), ("bar" / "Bar"_Type)));

  CHECK_PARSER("struct Foo{T}()\nend", StructDecl("Foo").type_arg("T"));
  CHECK_PARSER("struct Foo{T}(t T)\nend", StructDecl("Foo")("t" / "T"_Type).type_arg("T"));

  CHECK_PARSER("struct Foo()\ndef method()\nend\nend", StructDecl("Foo").body(FnDecl("method")));

  CHECK_PARSER("struct Foo() is Bar\nend", StructDecl("Foo").implements("Bar"_Type));
  CHECK_PARSER("struct Foo is Bar\nend", StructDecl("Foo").implements("Bar"_Type));
}

TEST_CASE("Parse interface declaration", "[parse][interface]") {
  CHECK_PARSER("interface Foo()\nend", StructDecl("Foo").interface());
  CHECK_PARSER("interface Foo\nend", StructDecl("Foo").interface());
  CHECK_PARSER("interface Foo(i I32)\nend", StructDecl("Foo")("i" / "I32"_Type).interface());
  CHECK_PARSER("interface Foo()\ndef method()\nend\nend", StructDecl("Foo").body(FnDecl("method")).interface());
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
