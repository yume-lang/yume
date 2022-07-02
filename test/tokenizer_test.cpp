#include "./test_common.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {
constexpr auto token_comparison = [](const yume::Token& a, const yume::Token& b) -> bool {
  return a.m_type == b.m_type && a.m_payload == b.m_payload;
};

template <typename... Ts> auto EqualsTokens(Ts... ts) {
  return EqualsRangeMatcher<std::vector<yume::Token>, decltype(token_comparison)>{{ts...}};
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

template <bool FilterSkip> auto tkn(const std::string& str) -> std::vector<Token> {
  static const std::string test_filename = "<test>";
  auto in_stream = std::stringstream(str);
  if (FilterSkip)
    return tokenize(in_stream, test_filename);
  return tokenize_preserve_skipped(in_stream, test_filename);
}
} // namespace

#define CHECK_TOKENIZER(body, ...) CHECK_THAT(tkn<true>(body), EqualsTokens(__VA_ARGS__))
#define CHECK_TOKENIZER_THROWS(body) CHECK_THROWS(tkn<true>(body))
#define CHECK_TOKENIZER_PRESERVED(body, ...) CHECK_THAT(tkn<false>(body), EqualsTokens(__VA_ARGS__))

using enum yume::Token::Type;

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

TEST_CASE("Tokenize characters", "[token]") {
  CHECK_TOKENIZER("?a", "a"_Char);
  CHECK_TOKENIZER("??", "?"_Char);
  CHECK_TOKENIZER("?\\0", "\0"_Char);
  CHECK_TOKENIZER("?\\\\", "\\"_Char);
}

TEST_CASE("Tokenize string literals", "[token]") {
  CHECK_TOKENIZER(R"("hi")", R"(hi)"_Literal);
  CHECK_TOKENIZER(R"("h\"i")", R"(h"i)"_Literal);
  CHECK_TOKENIZER(R"("h\ni")", "h\ni"_Literal);
  CHECK_TOKENIZER(R"("h\\i")", R"(h\i)"_Literal);
  CHECK_TOKENIZER(R"("foo" "bar")", "foo"_Literal, "bar"_Literal);
}

TEST_CASE("Tokenize operators/symbols", "[token]") {
  for (std::string i :
       {"=", "<", ">", "+", "-", "*", "/", "//", "(", ")", "==", "!=", "!", ",", ".", ":", "%", "[", "]"}) {
    CHECK_TOKENIZER(i, Token(Symbol, make_atom(i)));
  }

  CHECK_TOKENIZER("[]", "["_Symbol, "]"_Symbol);
}

TEST_CASE("Tokenize invalid tokens", "[token][throws]") { CHECK_TOKENIZER_THROWS("`"); }

#undef CHECK_TOKENIZER
#undef CHECK_TOKENIZER_THROWS
#undef CHECK_TOKENIZER_PRESERVED
