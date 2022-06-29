#include "./test_common.hpp"
#include "token.hpp"
#include <catch2/catch_test_macros.hpp>

#define TOKEN1(type) yume::Token(yume::Token::Type::type)
#define TOKEN2(type, payload) yume::Token(yume::Token::Type::type, yume::make_atom(payload))
#define GET_TOKEN(_1, _2, NAME, ...) NAME
#define TOKEN(...) GET_TOKEN(__VA_ARGS__, TOKEN2, TOKEN1)(__VA_ARGS__)
#define REQUIRE_TOKENIZER_GEN(m, body, ...)                                                                            \
  {                                                                                                                    \
    auto _in = std::string(body);                                                                                      \
    auto _in_stream = std::stringstream(_in);                                                                          \
    auto _tokens = yume::m(_in_stream, "<test>");                                                                      \
    REQUIRE_THAT(_tokens, EqualsTokens(__VA_ARGS__));                                                                  \
  }
#define REQUIRE_TOKENIZER(...) REQUIRE_TOKENIZER_GEN(tokenize, __VA_ARGS__)
#define REQUIRE_TOKENIZER_PRESERVED(...) REQUIRE_TOKENIZER_GEN(tokenize_preserve_skipped, __VA_ARGS__)

TEST_CASE("Tokenization") {
  REQUIRE_TOKENIZER("");
  REQUIRE_TOKENIZER("123", TOKEN(Number, "123"));
  REQUIRE_TOKENIZER(" ");
  REQUIRE_TOKENIZER_PRESERVED(" ", TOKEN(Skip, " "));
}
