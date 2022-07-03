#include "token.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace yume {
#if __cpp_lib_ranges >= 201911L
static constexpr auto any_of = std::ranges::any_of;
#else
struct any_of_fn {
  template <std::input_iterator I, std::sentinel_for<I> S, std::indirect_unary_predicate<I> Pred>
  constexpr auto operator()(I first, S last, Pred pred) const -> bool {
    return std::find_if(first, last, std::ref(pred)) != last;
  }

  template <std::ranges::input_range R, std::indirect_unary_predicate<std::ranges::iterator_t<R>> Pred>
  constexpr auto operator()(R&& r, Pred pred) const -> bool {
    return operator()(std::ranges::begin(r), std::ranges::end(r), std::ref(pred));
  }
};

static constexpr any_of_fn any_of;
#endif

struct TokenState {
  bool valid;
  char c;
  std::size_t index;
  std::stringstream& stream;
};
using char_raw_fn = int(int);
using char_fn = std::function<bool(TokenState&)>;

/// A criterion for classifying a stream of characters as a specific type of token \link Token::Type
/**
 * TODO: requires doc update regarding TokenState (and TokenState itself needs docs)
 *
 * The criterion is a function taking three arguments:
 * an `int` representing the current character being evaluated, a second `int` representing the
 * index of this character relative to the current character sequence (that is, since the end of the previous token),
 * and a `std::stringstream`, where matching characters should be written to. The function should return a `bool`,
 * whether or not this character is viable as this type of token. As soon as `false` is returned, this token ends and
 * the process is repeated.
 *
 * Note that while usually the same character (the first `int` argument) should be appended, tokens such as string
 * literals may include escapes and thus output the corresponding unescaped value.
 */
struct Characteristic {
  char_fn fn;
  Token::Type type;

  /// Criterion doesn't take a stringstream: if `true` is returned the same character is appended. This is usually
  /// preferred.
  Characteristic(Token::Type type, const std::function<bool(int, int)>& fn)
      : fn([fn](TokenState& state) {
          bool b = fn(state.c, static_cast<int>(state.index));
          if (b)
            state.stream.put(state.c);
          return b;
        }),
        type(type) {}
  Characteristic(Token::Type type, char_fn fn) : fn(std::move(fn)), type(type) {}
};

/// Contains the state while the tokenizer is running, such as the position within the file currently being read
struct Tokenizer {
  vector<Token> m_tokens{};
  std::istream& m_in;
  char m_last;
  int m_count{};
  int m_line = 1;
  int m_col = 1;
  const char* m_source_file;

  /// Words consist of alphanumeric characters, or underscores, but *must* begin with a letter.
  constexpr static const auto is_word = [](int c, int i) {
    return (i == 0 && std::isalpha(c) != 0) || std::isalnum(c) != 0 || c == '_';
  };

  /// Strings are delimited by double quotes `"` and may contain escapes.
  constexpr static const auto is_str = [end = false, escape = false](TokenState& state) mutable {
    if (end)
      return false;

    state.valid = false;
    if (state.index == 0)
      return state.c == '"';

    if (state.c == '\\' && !escape) {
      escape = true;
    } else if (state.c == '"' && !escape && !end) {
      end = true;
      state.valid = true;
    } else {
      if (escape && state.c == 'n')
        state.stream.put('\n');
      else
        state.stream.put(state.c);

      escape = false;
    }
    return true;
  };

  /// Chars begin with a question mark `?` and may contain escapes.
  constexpr static const auto is_char = [escape = false](TokenState& state) mutable {
    state.valid = false;
    if (state.index == 0)
      return state.c == '?';
    if (state.index == 1) {
      if (state.c == '\\')
        escape = true;
      else
        state.stream.put(state.c);
      state.valid = true;
      return true;
    }
    if (state.index == 2 && escape) {
      if (state.c == '0')
        state.stream.put('\x00');
      else
        state.stream.put(state.c);
      state.valid = true;
      return true;
    }
    return false;
  };

  /// Comments begin with an octothorpe `#` and last until the end of the line.
  constexpr static const auto is_comment = [](char c, int i) { return (i == 0 && c == '#') || (i > 0 && c != '\n'); };

  /// Generate a criterion matching a single character from any within the string `chars`.
  constexpr static const auto is_any_of = [](string chars) {
    return [checks = move(chars)](char c, int i) { return i == 0 && checks.find(c) != string::npos; };
  };

  /// Generate a criterion matching the string exactly.
  constexpr static const auto is_exactly = [](string str) {
    return [s = move(str)](TokenState& state) {
      if (state.index == s.size())
        return false;
      state.valid = state.index == s.size() - 1;

      bool matches = s[state.index] == state.c;
      if (matches) {
        state.stream.put(state.c);
      }

      return matches;
    };
  };

  /// Generate a criterion from a libc function from the `is...` family of functions, such as `isdigit`.
  constexpr static const auto is_c = [](char_raw_fn fn) {
    return [=](char c, [[maybe_unused]] int i) { return fn(c) != 0; };
  };

  void tokenize() {

    while (!m_in.eof()) {
      if (!select_characteristic({
              {Token::Type::Separator, is_exactly("\n")},
              {Token::Type::Skip, is_c(isspace)},
              {Token::Type::Skip, is_comment},
              {Token::Type::Number, is_c(isdigit)},
              {Token::Type::Literal, is_str},
              {Token::Type::Char, is_char},
              {Token::Type::Word, is_word},
              {Token::Type::Symbol, is_exactly("==")},
              {Token::Type::Symbol, is_exactly("!=")},
              {Token::Type::Symbol, is_exactly("//")},
              {Token::Type::Symbol, is_any_of(R"(()[]<>=:#%-+.,!/*\)")},
          })) {
        string message = "Tokenizer didn't recognize ";
        message += m_last;
        throw std::runtime_error(message);
      }
      m_count++;
    }
  }

  Tokenizer(std::istream& in, const char* source_file) : m_in(in), m_last(next()), m_source_file(source_file) {}

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

  /// Find the first criterion for which the current character is viable as the first character, then consume tokens
  /// until the criterion becomes false. The result is appended to the current list of tokens `m_tokens`.
  ///
  /// \returns `false` if no criterion matched.
  /// \sa is_characteristic, consume_characteristic
  auto select_characteristic(std::initializer_list<Characteristic> list) -> bool {
    int begin_line = m_line;
    int begin_col = m_col;
    char begin_last = m_last;
    auto begin_position = m_in.tellg();

    return any_of(list, [&](const auto& c) {
      m_line = begin_line;
      m_col = begin_col;
      m_last = begin_last;
      m_in.seekg(begin_position);
      m_in.clear();

      auto stream = std::stringstream{};
      auto state = TokenState{true, m_last, 0, stream};
      if (c.fn(state)) {
        auto [atom, end_line, end_col] = consume_characteristic(c.fn, state);
        if (state.valid) {
          m_tokens.emplace_back(c.type, atom, m_count, Loc{begin_line, begin_col, end_line, end_col, m_source_file});
          return true;
        }
      }
      return false;
    });
  }

  /// Consume characters until the criterion becomes false. Note that the first character is assumed to already be
  /// matched by `is_characteristic`
  /// \returns `Atom` containing the payload of the matched token, and the line and col number it stopped on.
  auto consume_characteristic(const char_fn& fun, TokenState& state) -> std::tuple<Atom, int, int> {
    state.index++;
    int end_line = m_line;
    int end_col = m_col;
    next();
    state.c = m_last;
    while (!m_in.eof() && fun(state)) {
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
  return tokenizer.m_tokens;
}

auto tokenize(std::istream& in, const string& source_file) -> vector<Token> {
  vector<Token> original = tokenize_preserve_skipped(in, source_file);
  vector<Token> filtered{};
  filtered.reserve(original.size());
  std::copy_if(original.begin(), original.end(), std::back_inserter(filtered),
               [](const Token& t) { return t.m_type != Token::Type::Skip; });
  return filtered;
}

auto operator<<(llvm::raw_ostream& os, const Token& token) -> llvm::raw_ostream& {
  os << "Token" << llvm::format_decimal(token.m_i, 4) << '(';
  const auto& loc = token.m_loc;
  os << loc.to_string() << ",";
  os << Token::type_name(token.m_type);
  if (token.m_payload.has_value()) {
    os << ",\"";
    os.write_escaped(string(*token.m_payload));
    os << '\"';
  }
  os << ")";
  return os;
}
} // namespace yume
