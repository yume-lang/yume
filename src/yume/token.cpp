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
using char_raw_fn = int(int);
using char_fn = std::function<bool(int, int, std::stringstream&)>;

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

/// A criterion for classifying a stream of characters as a specific type of token \link Token::Type
/**
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
  char_fn m_fn;
  Token::Type m_type;

  /// Criterion doesn't take a stringstream: if `true` is returned the same character is appended. This is usually
  /// preferred.
  Characteristic(Token::Type type, const std::function<bool(int, int)>& fn)
      : m_fn([fn](int c, int i, std::stringstream& stream) {
          bool b = fn(c, i);
          if (b)
            stream.put((char)c);
          return b;
        }),
        m_type(type) {}
  Characteristic(Token::Type type, char_fn fn) : m_fn(std::move(fn)), m_type(type) {}
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
  constexpr static const auto is_str = [end = false, escape = false](char c, int i, std::stringstream& stream) mutable {
    if (i == 0)
      return c == '"';
    if (end)
      return false;

    if (c == '\\' && !escape) {
      escape = true;
    } else if (c == '"' && !escape && !end) {
      end = true;
    } else {
      if (escape && c == 'n')
        stream.put('\n');
      else
        stream.put((char)c);

      escape = false;
    }
    return true;
  };

  /// Chars begin with a question mark `?` and may contain escapes.
  constexpr static const auto is_char = [escape = false](char c, int i, std::stringstream& stream) mutable {
    if (i == 0)
      return c == '?';
    if (i == 1) {
      if (c == '\\')
        escape = true;
      else
        stream.put((char)c);
      return true;
    }
    if (i == 2 && escape) {
      if (c == '0')
        stream.put('\x00');
      else
        stream.put((char)c);
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

  // TODO: fix below bug (see !mustfail case in tokenizer test)
  /// Generate a criterion matching the string exactly.
  /// \bug Partial matches also work...
  constexpr static const auto is_exactly = [](string str) {
    return [s = move(str)](char c, int i) { return s[i] == c; };
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
              {Token::Type::Symbol, is_any_of(R"(()[]<>=:#"%-+.,!?/*\)")},
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

  /// Check if the current character is viable as the first character of the given criterion.
  [[nodiscard]] auto is_characteristic(const char_fn& fun, std::stringstream& stream) const -> bool {
    return fun(m_last, 0, stream);
  }

  /// Find the first criterion for which the current character is viable as the first character, then consume tokens
  /// until the criterion becomes false. The result is appended to the current list of tokens `m_tokens`.
  ///
  /// \returns `false` if no criterion matched.
  /// \sa is_characteristic, consume_characteristic
  auto select_characteristic(std::initializer_list<Characteristic> list) -> bool {
    int begin_line = m_line;
    int begin_col = m_col;
    return any_of(list, [&](const auto& c) {
      auto stream = std::stringstream{};
      if (is_characteristic(c.m_fn, stream)) {
        auto [atom, end_line, end_col] = consume_characteristic(c.m_fn, stream);
        m_tokens.emplace_back(c.m_type, atom, m_count, Loc{begin_line, begin_col, end_line, end_col, m_source_file});
        return true;
      }
      return false;
    });
  }

  /// Consume characters until the criterion becomes false. Note that the first character is assumed to already be
  /// matched by `is_characteristic`
  /// \returns `Atom` containing the payload of the matched token, and the line and col number it stopped on.
  auto consume_characteristic(const char_fn& fun, std::stringstream& out) -> std::tuple<Atom, int, int> {
    int i = 1;
    int end_line = m_line;
    int end_col = m_col;
    next();
    while (!m_in.eof() && fun(m_last, i, out)) {
      i++;
      end_line = m_line;
      end_col = m_col;
      next();
    }

    return {make_atom(out.str()), end_line, end_col};
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
