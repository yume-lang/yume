//
// Created by rymiel on 5/8/22.
//

#ifndef YUME_CPP_TOKEN_HPP
#define YUME_CPP_TOKEN_HPP

#include "util.hpp"
#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace yume {
struct Loc {
  const int begin_line;
  const int begin_col;
  const int end_line;
  const int end_col;
  const char* const file;

  constexpr auto operator<=>(const Loc& other) const noexcept = default;

  constexpr auto operator+(const Loc& other) const noexcept -> Loc {
    assert(other.file == file);
    auto [min_begin_line, min_begin_col] =
        std::min(std::pair{begin_line, begin_col}, std::pair{other.begin_line, other.begin_col});
    auto [max_end_line, max_end_col] = std::max(std::pair{end_line, end_col}, std::pair{other.end_line, other.end_col});
    return Loc{min_begin_line, min_begin_col, max_end_line, max_end_col, file};
  }

  [[nodiscard]] auto to_string() const -> string {
    std::stringstream ss{};
    if (file != nullptr) {
      const auto file_s = string(file);

      auto delim = file_s.rfind('/');
      ss << file_s.substr(delim == string::npos ? 0 : delim + 1);
    }

    if (begin_line == 0 || end_line == 0) {
      ss << ":?";
    } else {
      ss << ':' << begin_line << ':' << begin_col;
      if (end_line != begin_line) {
        ss << ' ' << end_line << ':' << end_col;
      } else if (end_col != begin_col) {
        ss << " :" << end_col;
      }
    }
    return ss.str();
  }
};

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
  }

  using Payload = optional<Atom>;

  Type m_type;
  Payload m_payload;
  int m_i = -1;
  Loc m_loc{};

  [[nodiscard]] inline constexpr auto is_keyword(const Atom& str) const -> bool {
    return m_type == Type::Word && m_payload == str;
  }

  explicit constexpr inline Token(Type type) : m_type(type) {}
  inline constexpr Token(Type type, Payload payload) : m_type(type), m_payload(payload) {}
  inline constexpr Token(Type type, Payload payload, int i, Loc loc)
      : m_type(type), m_payload(payload), m_i{i}, m_loc{loc} {}

  friend auto operator<<(std::ostream& os, const Token& token) -> std::ostream&;
};

auto tokenize_preserve_skipped(std::istream& in, const string& source_file) -> vector<Token>;

auto tokenize(std::istream& in, const string& source_file) -> vector<Token>;
} // namespace yume

#endif // YUME_CPP_TOKEN_HPP
