//
// Created by rymiel on 5/8/22.
//

#include "token.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <llvm/Support/raw_os_ostream.h>
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
inline constexpr auto any_of = std::ranges::any_of;
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

inline constexpr any_of_fn any_of;

#endif

struct Characteristic {
  char_fn m_fn;
  Token::Type m_type;

  Characteristic(Token::Type type, const std::function<bool(int, int)>& fn)
      : m_fn([fn](int c, int i, std::stringstream& stream) {
          bool b = fn(c, i);
          if (b) {
            stream.put((char)c);
          }
          return b;
        }),
        m_type(type) {}
  Characteristic(Token::Type type, char_fn fn) : m_fn(std::move(fn)), m_type(type) {}
};

struct Tokenizer {
  vector<Token> m_tokens{};
  std::istream& m_in;
  char m_last;
  int m_count{};
  int m_line = 1;
  int m_col = 1;
  const char* m_source_file;

  constexpr static const auto is_word = [](int c, int i) {
    return (i == 0 && std::isalpha(c) != 0) || std::isalnum(c) != 0 || c == '_';
  };
  constexpr static const auto is_str = [end = false, escape = false](char c, int i, std::stringstream& stream) mutable {
    if (i == 0) {
      return c == '"';
    }
    if (end) {
      return false;
    }
    if (c == '\\' && !escape) {
      escape = true;
    } else if (c == '"' && !end) {
      end = true;
    } else {
      if (escape && c == 'n') {
        stream.put('\n');
      } else {
        stream.put((char)c);
      }
      escape = false;
    }
    return true;
  };
  constexpr static const auto is_char = [escape = false](char c, int i, std::stringstream& stream) mutable {
    if (i == 0) {
      return c == '?';
    }
    if (i == 1) {
      if (c == '\\') {
        escape = true;
      } else {
        stream.put((char)c);
      }
      return true;
    }
    if (i == 2 && escape) {
      stream.put((char)c);
      return true;
    }
    return false;
  };
  constexpr static const auto is_comment = [](char c, int i) { return (i == 0 && c == '#') || (i > 0 && c != '\n'); };
  constexpr static const auto is_any_of = [](string chars) {
    return [checks = move(chars)](char c, int i) { return i == 0 && checks.find(c) != string::npos; };
  };
  constexpr static const auto is_exactly = [](string str) {
    return [s = move(str)](char c, int i) { return s[i] == c; };
  };
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

  [[nodiscard]] auto is_characteristic(const char_fn& fun, std::stringstream& stream) const -> bool {
    return fun(m_last, 0, stream);
  }

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

  auto consume_characteristic(const char_fn& fun, std::stringstream& out) -> std::tuple<Atom, int, int> {
    int i = 1;
    int end_line = m_line;
    int end_col = m_col;
    next();
    while (fun(m_last, i, out) && !m_in.eof()) {
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

auto operator<<(std::ostream& std_os, const Token& token) -> std::ostream& {
  auto os = llvm::raw_os_ostream(std_os);
  std_os << "Token" << std::setfill('0') << std::setw(4) << token.m_i << '(';
  const auto& loc = token.m_loc;
  os << loc.to_string() << ",\t";
  os << Token::type_name(token.m_type);
  if (token.m_payload.has_value()) {
    os << ",\t\"";
    os.write_escaped(string(*token.m_payload));
    os << '\"';
  }
  os << ")";
  return std_os;
}
} // namespace yume
