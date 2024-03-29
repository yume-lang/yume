#include "./test_common.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <llvm/Support/raw_ostream.h>

namespace {
constexpr auto token_comparison = [](const yume::Token& a, const yume::Token& b) -> bool {
  return a.type == b.type && a.payload == b.payload;
};

template <typename... Ts> auto equals_tokens(Ts... ts) {
  return EqualsRangeMatcher<std::vector<yume::Token>, decltype(token_comparison)>{
      {ts..., yume::Token{yume::Token::Type::EndOfFile, std::nullopt}}};
}

using yume::make_atom;
using yume::Token;
using yume::tokenize;
using yume::tokenize_preserve_skipped;
using enum yume::Token::Type;

auto operator""_Skip(const char* str, size_t size) -> Token { return {Skip, make_atom({str, size})}; }
auto operator""_Char(const char* str, size_t size) -> Token { return {Char, make_atom({str, size})}; }
auto operator""_Number(const char* str, size_t size) -> Token { return {Number, make_atom({str, size})}; }
auto operator""_Literal(const char* str, size_t size) -> Token { return {Literal, make_atom({str, size})}; }
auto operator""_Separator(const char* str, size_t size) -> Token { return {Separator, make_atom({str, size})}; }
auto operator""_Symbol(const char* str, size_t size) -> Token { return {Symbol, make_atom({str, size})}; }
auto operator""_Word(const char* str, size_t size) -> Token { return {Word, make_atom({str, size})}; }

auto tkn(const std::string& str) -> std::vector<Token> {
  static const std::string TEST_FILENAME = "<test>";
  auto in_stream = std::stringstream(str);
  return tokenize(in_stream, TEST_FILENAME);
}

auto tkn_preserve(const std::string& str) -> std::vector<Token> {
  static const std::string TEST_FILENAME = "<test>";
  auto in_stream = std::stringstream(str);
  return tokenize_preserve_skipped(in_stream, TEST_FILENAME);
}
} // namespace

#define CHECK_TOKENIZER(body, ...) CHECK_THAT(tkn(body), equals_tokens(__VA_ARGS__))
#define CHECK_TOKENIZER_THROWS(body) CHECK_THROWS(tkn(body))
#define CHECK_TOKENIZER_PRESERVED(body, ...) CHECK_THAT(tkn_preserve(body), equals_tokens(__VA_ARGS__))

using enum yume::Token::Type;
using namespace std::string_literals;

TEST_CASE("Tokenize whitespace", "[token]") {
  CHECK_TOKENIZER("");

  CHECK_TOKENIZER(" ");
  CHECK_TOKENIZER_PRESERVED(" ", " "_Skip);

  CHECK_TOKENIZER("\n", "\n"_Separator);
}

TEST_CASE("Tokenize literals", "[token]") {
  CHECK_TOKENIZER("hello", "hello"_Word);
  CHECK_TOKENIZER("true", "true"_Word);
  CHECK_TOKENIZER("false", "false"_Word);

  CHECK_TOKENIZER("a b", "a"_Word, "b"_Word);
  CHECK_TOKENIZER_PRESERVED("a b", "a"_Word, " "_Skip, "b"_Word);
  CHECK_TOKENIZER("a\nb", "a"_Word, "\n"_Separator, "b"_Word);
  CHECK_TOKENIZER("a\n  b", "a"_Word, "\n"_Separator, "b"_Word);
}

TEST_CASE("Tokenize comments", "[token]") {
  CHECK_TOKENIZER("# goodbye");
  CHECK_TOKENIZER("# goodbye\n", "\n"_Separator);
  CHECK_TOKENIZER("# goodbye\na", "\n"_Separator, "a"_Word);
  CHECK_TOKENIZER_PRESERVED("# goodbye", "# goodbye"_Skip);
  CHECK_TOKENIZER_PRESERVED("# goodbye\n", "# goodbye"_Skip, "\n"_Separator);
  CHECK_TOKENIZER_PRESERVED("# goodbye\na", "# goodbye"_Skip, "\n"_Separator, "a"_Word);
}

TEST_CASE("Tokenize numbers", "[token]") {
  CHECK_TOKENIZER("0", "0"_Number);
  CHECK_TOKENIZER("123", "123"_Number);
}

TEST_CASE("Tokenize hex numbers", "[token]") {
  CHECK_TOKENIZER("0x0", "0x0"_Number);
  CHECK_TOKENIZER("0xfa123", "0xfa123"_Number);
}

TEST_CASE("Tokenize invalid hex numbers", "[token][throws]") {
  CHECK_TOKENIZER_THROWS("0x");
  CHECK_TOKENIZER_THROWS("0xg");
}

TEST_CASE("Tokenize characters", "[token]") {
  CHECK_TOKENIZER("?a", "a"_Char);
  CHECK_TOKENIZER("??", "?"_Char);
  CHECK_TOKENIZER("?\\0", "\0"_Char);
  CHECK_TOKENIZER("?\\\\", "\\"_Char);
  CHECK_TOKENIZER("?a ", "a"_Char);
  CHECK_TOKENIZER("?? ", "?"_Char);
  CHECK_TOKENIZER("?\\0 ", "\0"_Char);
  CHECK_TOKENIZER("?\\\\ ", "\\"_Char);
}

TEST_CASE("Tokenize string literals", "[token]") {
  CHECK_TOKENIZER(R"("hi")", R"(hi)"_Literal);
  CHECK_TOKENIZER(R"("h\"i")", R"(h"i)"_Literal);
  CHECK_TOKENIZER(R"("h\ni")", "h\ni"_Literal);
  CHECK_TOKENIZER(R"("h\\i")", R"(h\i)"_Literal);
  CHECK_TOKENIZER(R"("foo" "bar")", "foo"_Literal, "bar"_Literal);
}

TEST_CASE("Tokenize operators/symbols", "[token]") {
  for (const std::string i :
       {"=", "<", ">", "+", "-", "*", "/", "//", "(", ")", "==", "!=", "!", ",", ".", ":", "::", "%", "[", "]", "&"}) {
    CHECK_TOKENIZER(i, Token(Symbol, make_atom(i)));
  }

  CHECK_TOKENIZER("[]", "["_Symbol, "]"_Symbol);
  CHECK_TOKENIZER("{}", "{"_Symbol, "}"_Symbol);
  CHECK_TOKENIZER(": :", ":"_Symbol, ":"_Symbol);
  CHECK_TOKENIZER("foo::bar", "foo"_Word, "::"_Symbol, "bar"_Word);
}

TEST_CASE("Token stringification", "[token][str]") {
  std::string str;
  llvm::raw_string_ostream ss(str);

  auto in_stream = std::stringstream("foo");
  auto filename = "<filename>"s;
  auto tokens = yume::tokenize(in_stream, filename);

  REQUIRE(tokens.size() == 2);
  ss << tokens[0];

  CHECK(str == "Token   0(<filename>:1:1 :3,Word,\"foo\")");
}

TEST_CASE("Tokenize invalid tokens", "[token][throws]") { CHECK_TOKENIZER_THROWS("`"); }

TEST_CASE("Tokenize empty char", "[token][throws]") { CHECK_TOKENIZER_THROWS("?"); }

TEST_CASE("Tokenize incomplete string", "[token][throws]") {
  CHECK_TOKENIZER_THROWS(R"("who stole the closing quote)");
}
