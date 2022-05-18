//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_TOKEN_HPP
#define YUME_CPP_TOKEN_HPP

#include "util.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yume {
struct Token {
  enum struct Type { Word, Skip, Symbol, Literal, Number, Separator };
  static auto inline constexpr type_name(Type type) -> const char* {
    switch (type) {
    case Token::Type::Word: return "Word";
    case Token::Type::Skip: return "Skip";
    case Token::Type::Symbol: return "Symbol";
    case Token::Type::Literal: return "Literal";
    case Token::Type::Number: return "Number";
    case Token::Type::Separator: return "Separator";
    }
  };

  using Payload = optional<Atom>;

  Type m_type;
  Payload m_payload;
  int m_i = -1;

  [[nodiscard]] inline constexpr auto is_keyword(const Atom& str) const -> bool {
    return m_type == Type::Word && m_payload == str;
  }

  explicit constexpr inline Token(Type type) : m_type(type) {}
  inline constexpr Token(Type type, Payload payload) : m_type(type), m_payload(payload) {}
  inline constexpr Token(Type type, Payload payload, int i) : m_type(type), m_payload(payload), m_i{i} {}

  friend auto operator<<(std::ostream& os, const Token& token) -> std::ostream&;
};

auto tokenize_preserve_skipped(std::istream& in) -> vector<Token>;

auto tokenize(std::istream& in) -> vector<Token>;
} // namespace yume

#endif // YUME_CPP_TOKEN_HPP
