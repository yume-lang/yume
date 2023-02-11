#include "token.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace yume {
using char_raw_fn = bool(char);
struct TokenState {
  bool valid;
  char c;
  size_t index;
  llvm::raw_string_ostream& stream;

  auto validate(bool val = true) -> bool {
    valid |= val;
    return val;
  }

  auto accept(bool ok) -> bool {
    if (ok)
      stream.write(c);
    return ok;
  }

  auto accept() -> bool {
    stream.write(c);
    return true;
  }

  auto accept(char chr) -> bool { return accept(c == chr); }
  auto accept_not(char chr) -> bool { return accept(c != chr); }
  auto accept(char_raw_fn fn) -> bool { return accept(fn(c)); }

  auto accept_validate(auto x) -> bool { return validate(accept(x)); }
};

template <typename T>
concept char_fn = requires(T t, TokenState& state) {
                    { t(state) } -> std::same_as<bool>;
                  };

/// Contains the state while the tokenizer is running, such as the position within the file currently being read
class Tokenizer {
  vector<Token> m_tokens{};
  std::istream& m_in;
  char m_last;
  bool m_error_state = false;
  int m_count{};
  int m_line = 1;
  int m_col = 1;
  int m_begin_line = 1;
  int m_begin_col = 1;
  const char* m_source_file;
  std::string m_stream_buffer;

  static auto unescape(char c) -> char {
    switch (c) {
    case '0': return '\x00';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    default: return c;
    }
  }

public:
  /// Words consist of alphanumeric characters, or underscores, but *must* begin with a letter.
  constexpr static const auto is_word = [](TokenState& state) {
    if (state.index == 0)
      return state.accept_validate(llvm::isAlpha(state.c) || state.c == '_');
    return state.accept_validate(llvm::isAlnum(state.c) || state.c == '_');
  };

  /// Strings are delimited by double quotes `"` and may contain escapes.
  constexpr static const auto is_str = [end = false, escape = false](TokenState& state) mutable {
    if (end)
      return false;

    if (state.index == 0)
      return state.c == '"';

    if (state.c == '\\' && !escape) {
      escape = true;
    } else if (state.c == '"' && !escape && !end) {
      end = true;
      state.validate();
    } else if (escape) {
      state.stream.write(unescape(state.c));
      escape = false;
    } else {
      state.stream.write(state.c);
    }
    return true;
  };

  /// Character literals begin with a question mark `?` and may contain escapes.
  constexpr static const auto is_char_lit = [escape = false](TokenState& state) mutable {
    if (state.index == 0)
      return state.c == '?';

    if (state.index == 1) {
      if (state.c == '\\')
        escape = true;
      else
        state.stream.write(state.c);
      return state.validate();
    }
    if (state.index == 2 && escape) {
      state.stream.write(unescape(state.c));
      return state.validate();
    }
    return false;
  };

  /// Comments begin with an octothorpe `#` and last until the end of the line.
  constexpr static const auto is_comment = [](TokenState& state) {
    if (state.index == 0)
      return state.accept_validate('#');
    return state.accept_validate(state.c != '\n');
  };

  /// This matches both regular numbers (0-9), and hex number. Hex numbers begin with `0x`, and consist of any of 0-9,
  /// a-f or A-F. If the first character is a 0, is is ambiguous and must be checked further.
  constexpr static const auto is_num_or_hex_num = [possibly_hex = false](TokenState& state) mutable {
    if (state.index == 0 && state.c == '0') {
      possibly_hex = true;
      return state.accept_validate(true);
    }
    if (possibly_hex && state.index == 1) {
      if (state.c == 'x') {
        // Invalidate, since we need a character after the x
        state.valid = false;
        return state.accept();
      }
      possibly_hex = false;
    }
    if (possibly_hex)
      return state.accept_validate(llvm::isHexDigit);
    return state.accept_validate(llvm::isDigit);
  };

  /// Generate a criterion matching a single character from any within the string `checks`.
  constexpr static const auto is_any_of = [](string_view checks) {
    return [checks](TokenState& state) {
      return state.index == 0 && state.accept_validate(checks.find(state.c) != string::npos);
    };
  };

  /// Generate a criterion matching one or both of the character
  constexpr static const auto is_partial = [](char c1, char c2) {
    return [c1, c2](TokenState& state) {
      if (state.index == 0)
        return state.accept_validate(c1);
      if (state.index == 1)
        return state.accept_validate(c2);
      return false;
    };
  };

  /// Generate a criterion matching the singular character.
  constexpr static const auto is_char = [](char chr) {
    return [chr](TokenState& state) { return state.index == 0 && state.accept_validate(chr); };
  };

  void tokenize() {

    while (!m_in.eof()) {
      m_begin_line = m_line;
      m_begin_col = m_col;
      // m_begin_last = m_last;
      // m_begin_position = m_in.tellg();

      if (check_characteristic(Token::Type::Separator, is_char('\n')) ||
          check_characteristic(Token::Type::Skip, llvm::isSpace) ||
          check_characteristic(Token::Type::Skip, is_comment) ||
          check_characteristic(Token::Type::Number, is_num_or_hex_num) ||
          check_characteristic(Token::Type::Literal, is_str) ||   //
          check_characteristic(Token::Type::Char, is_char_lit) || //
          check_characteristic(Token::Type::Word, is_word) ||
          check_characteristic(Token::Type::Symbol, is_partial('=', '=')) || // = and ==
          check_characteristic(Token::Type::Symbol, is_partial('!', '=')) || // ! and !=
          check_characteristic(Token::Type::Symbol, is_partial('/', '/')) || // / and //
          check_characteristic(Token::Type::Symbol, is_partial(':', ':')) || // : and ::
          check_characteristic(Token::Type::Symbol, is_partial('-', '>')) || // - and ->
          check_characteristic(Token::Type::Symbol, is_partial('|', '|')) || // | and ||
          check_characteristic(Token::Type::Symbol, is_partial('&', '&')) || // & and &&
          check_characteristic(Token::Type::Symbol, is_any_of(R"(()[]{}<>%+.,*@$)"))) {

        if (!m_error_state)
          continue;
      }
      std::stringstream msg;
      msg << "Tokenizer didn't recognize '" << m_last << "' at " << m_source_file << ":" << m_line << ":" << m_col;
      throw std::runtime_error(msg.str());
      m_count++;
    }

    m_tokens.emplace_back(Token::Type::EndOfFile, std::nullopt, m_count,
                          Loc{m_line, m_col, m_line, m_col, m_source_file});
  }

  Tokenizer(std::istream& in, const char* source_file) : m_in(in), m_last(next()), m_source_file(source_file) {}

  [[nodiscard]] auto tokens() { return m_tokens; }

private:
  auto next() -> char {
    m_in.get(m_last);
    if (m_last == '\n') {
      m_line++;
      m_col = 0;
    } else {
      m_col++;
    }
    return m_last;
  }

  /// Determine if the criterion is viable with the current character as the first character, then consume tokens
  /// until the criterion becomes false. The result is appended to the current list of tokens `m_tokens`.
  ///
  /// \returns `true` if the first character is ok
  auto check_characteristic(Token::Type type, char_fn auto fn) -> bool {
    m_stream_buffer.clear();
    auto stream = llvm::raw_string_ostream{m_stream_buffer};
    auto state = TokenState{false, m_last, 0, stream};
    if (fn(state)) {
      auto [atom, end_line, end_col] = consume_characteristic(fn, state);
      if (state.valid) {
        m_tokens.emplace_back(type, atom, m_count, Loc{m_begin_line, m_begin_col, end_line, end_col, m_source_file});
        return true;
      }

      m_error_state = true;
      return true;
    }
    return false;
  }

  auto check_characteristic(Token::Type type, char_raw_fn* fn) -> bool {
    return check_characteristic(type, [fn](TokenState& state) { return state.accept_validate(fn); });
  }

  /// Consume characters until the criterion becomes false. Note that the first character is assumed to already be
  /// matched.
  /// \returns `Atom` containing the payload of the matched token, and the line and col number it stopped on.
  auto consume_characteristic(char_fn auto fn, TokenState& state) -> std::tuple<Atom, int, int> {
    state.index++;
    int end_line = m_line;
    int end_col = m_col;
    next();
    state.c = m_last;
    while (!m_in.eof() && fn(state)) {
      state.index++;
      end_line = m_line;
      end_col = m_col;
      next();
      state.c = m_last;
    }

    return {make_atom(state.stream.str()), end_line, end_col};
  }
};

auto tokenize_preserve_skipped(std::istream& in, const string& source_file) -> vector<Token> {
  auto tokenizer = Tokenizer(in, source_file.data());
  tokenizer.tokenize();
  return tokenizer.tokens();
}

auto tokenize(std::istream& in, const string& source_file) -> vector<Token> {
  vector<Token> original = tokenize_preserve_skipped(in, source_file);
  vector<Token> filtered{};
  filtered.reserve(original.size());
  std::copy_if(original.begin(), original.end(), std::back_inserter(filtered),
               [](const Token& t) { return t.type != Token::Type::Skip; });
  return filtered;
}

auto operator<<(llvm::raw_ostream& os, const Token& token) -> llvm::raw_ostream& {
  os << "Token" << llvm::format_decimal(token.index, 4) << '(';
  const auto& loc = token.loc;
  os << loc.to_string() << ",";
  os << Token::type_name(token.type);
  if (token.payload.has_value()) {
    os << ",\"";
    os.write_escaped(string(*token.payload));
    os << '\"';
  }
  os << ")";
  return os;
}
} // namespace yume
