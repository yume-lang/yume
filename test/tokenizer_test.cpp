#include "./test_common.hpp"
#include "token.hpp"
#include "util.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {
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

#define CHECK_TOKENIZER_GEN(m, body, ...) CHECK_THAT(tkn m(body), EqualsTokens(__VA_ARGS__))
#define CHECK_TOKENIZER(...) CHECK_TOKENIZER_GEN(<1>, __VA_ARGS__)
#define CHECK_TOKENIZER_PRESERVED(...) CHECK_TOKENIZER_GEN(<0>, __VA_ARGS__)

TEST_CASE("Tokenization") {
  using enum yume::Token::Type;

  CHECK_TOKENIZER("");

  CHECK_TOKENIZER(" ");
  CHECK_TOKENIZER_PRESERVED(" ", " "_Skip);

  CHECK_TOKENIZER("\n", "\n"_Separator);

  CHECK_TOKENIZER("hello", "hello"_Word);
  CHECK_TOKENIZER("true", "true"_Word);
  CHECK_TOKENIZER("false", "false"_Word);

  CHECK_TOKENIZER("a b", "a"_Word, "b"_Word);
  CHECK_TOKENIZER_PRESERVED("a b", "a"_Word, " "_Skip, "b"_Word);
  CHECK_TOKENIZER("a\nb", "a"_Word, "\n"_Separator, "b"_Word);
  CHECK_TOKENIZER("a\n  b", "a"_Word, "\n"_Separator, "b"_Word);

  CHECK_TOKENIZER("# goodbye");
  CHECK_TOKENIZER("# goodbye\n", "\n"_Separator);
  CHECK_TOKENIZER("# goodbye\na", "\n"_Separator, "a"_Word);
  CHECK_TOKENIZER_PRESERVED("# goodbye", "# goodbye"_Skip);
  CHECK_TOKENIZER_PRESERVED("# goodbye\n", "# goodbye"_Skip, "\n"_Separator);
  CHECK_TOKENIZER_PRESERVED("# goodbye\na", "# goodbye"_Skip, "\n"_Separator, "a"_Word);

  CHECK_TOKENIZER("0", "0"_Number);
  CHECK_TOKENIZER("123", "123"_Number);

  CHECK_TOKENIZER("?a", "a"_Char);
  CHECK_TOKENIZER("??", "?"_Char);
  CHECK_TOKENIZER("?\\0", "\0"_Char);
  CHECK_TOKENIZER("?\\\\", "\\"_Char);

  CHECK_TOKENIZER(R"("hi")", R"(hi)"_Literal);
  CHECK_TOKENIZER(R"("h\"i")", R"(h"i)"_Literal);
  CHECK_TOKENIZER(R"("h\\i")", R"(h\i)"_Literal);

  for (std::string i :
       {"=", "<", ">", "+", "-", "*", "/", "//", "(", ")", "==", "!=", "!", ",", ".", ":", "%", "[", "]"}) {
    CHECK_TOKENIZER(i, Token(Symbol, make_atom(i)));
  }

  CHECK_TOKENIZER("[]", "["_Symbol, "]"_Symbol);
}

#undef CHECK_TOKENIZER_GEN
#undef CHECK_TOKENIZER
#undef CHECK_TOKENIZER_PRESERVED
