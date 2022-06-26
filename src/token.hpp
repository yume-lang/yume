#pragma once

#include "util.hpp"
#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace yume {

/// Represents a location in source code, as a range starting at a line and column and ending at some other line and
/// column of some file.
/**
 * Lines are 1-indexed, meaning a line number of 0 as either the beginning or end represents an unknown location.
 * The range is inclusive, meaning a location representing just a single character would have its begin line and column
 * be equal to its end line and column. There is no way to store a location representing "zero characters".
 */
struct Loc {
  const int begin_line;
  const int begin_col;
  const int end_line;
  const int end_col;
  const char* const file;

  constexpr auto operator<=>(const Loc& other) const noexcept = default;

  /// \brief Create a new location representing the "union" of two locations.
  ///
  /// The new location will have the beginning line and column of whichever location has a beginning earlier in the
  /// file, and an ending line and column of whichever location has an ending later in the file.
  constexpr auto operator+(const Loc& other) const noexcept -> Loc {
    assert(other.file == file); // NOLINT
    auto [min_begin_line, min_begin_col] =
        std::min(std::pair{begin_line, begin_col}, std::pair{other.begin_line, other.begin_col});
    auto [max_end_line, max_end_col] = std::max(std::pair{end_line, end_col}, std::pair{other.end_line, other.end_col});
    return Loc{min_begin_line, min_begin_col, max_end_line, max_end_col, file};
  }

  [[nodiscard]] auto to_string() const -> string {
    std::stringstream ss{};
    if (file != nullptr) {
      ss << stem(file);
    }

    if (!valid()) {
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

  [[nodiscard]] auto valid() const -> bool { return begin_line > 0 && end_line > 0; }
};

/// A categorized token in source code, created by the tokenizer. These tokens are consumed by the lexer.
/**
 * Each token has a type, an associated payload (usually the text the token was created from) and a location \link Loc
 */
struct Token {
  enum struct Type {
    Word,     ///< Any form of keyword or identifier, essentially the "default" token type
    Skip,     ///< Tokens which should be ignored, i.e. insignificant whitespace
    Symbol,   ///< Special characters, such as those representing operators
    Literal,  ///< A string literal, enclosed in quotes
    Number,   ///< A number literal
    Char,     ///< A character literal, beginning with `?`
    Separator ///< A newline or a semicolon `;`
  };
  static auto inline constexpr type_name(Type type) -> const char* {
    using enum Token::Type;
    switch (type) {
    case Word: return "Word";
    case Skip: return "Skip";
    case Symbol: return "Symbol";
    case Literal: return "Literal";
    case Number: return "Number";
    case Char: return "Char";
    case Separator: return "Separator";
    }
  }

  using Payload = optional<Atom>;

  Type m_type;
  Payload m_payload;
  int m_i = -1;
  Loc m_loc{};

  [[nodiscard]] auto is_keyword(const Atom& str) const -> bool { return m_type == Type::Word && m_payload == str; }

  explicit constexpr Token(Type type) : m_type(type) {}
  constexpr Token(Type type, Payload payload) : m_type(type), m_payload(payload) {}
  constexpr Token(Type type, Payload payload, int i, Loc loc) : m_type(type), m_payload(payload), m_i{i}, m_loc{loc} {}

  friend auto operator<<(llvm::raw_ostream& os, const Token& token) -> llvm::raw_ostream&;
};

/// Consume the contents of the input stream and create corresponding tokens, preserving every token, including
/// whitespace. This is usually undesired.
/// \sa tokenize
auto tokenize_preserve_skipped(std::istream& in, const string& source_file) -> vector<Token>;

/// Consume the contents of the input stream and create corresponding tokens, ignoring insignificant whitespace
auto tokenize(std::istream& in, const string& source_file) -> vector<Token>;
} // namespace yume
