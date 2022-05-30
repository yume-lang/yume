//
// Created by rymiel on 5/8/22.
//

#include "token.hpp"
#include <algorithm>
#include <iostream>
#include <ranges>
#include <sstream>
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

  void tokenize() {
    using namespace std::literals::string_literals;
    auto is_word = [](int c, int i) { return (i == 0 && std::isalpha(c) != 0) || std::isalnum(c) != 0 || c == '_'; };
    auto is_str = [end = false, escape = false](char c, int i, std::stringstream& stream) mutable {
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
          stream.put(c);
        }
        escape = false;
      }
      return true;
    };
    auto is_comment = [](char c, int i) { return (i == 0 && c == '#') || (i > 0 && c != '\n'); };
    auto is_any_of = [](string chars) {
      return [checks = move(chars)](char c, int i) { return i == 0 && checks.find(c) != string::npos; };
    };
    auto is_exactly = [](string str) { return [s = move(str)](char c, int i) { return s[i] == c; }; };
    auto is_c = [](char_raw_fn fn) { return [=](char c, int i) { return fn(c) != 0; }; };

    int i = 0;
    while (!m_in.eof()) {
      if (!select_characteristic(
              {
                  {Token::Type::Separator, is_exactly("\n")                    },
                  {Token::Type::Skip,      is_c(isspace)                       },
                  {Token::Type::Skip,      is_comment                          },
                  {Token::Type::Number,    is_c(isdigit)                       },
                  {Token::Type::Literal,   is_str                              },
                  {Token::Type::Word,      is_word                             },
                  {Token::Type::Symbol,    is_exactly("==")                    },
                  {Token::Type::Symbol,    is_exactly("//")                    },
                  {Token::Type::Symbol,    is_any_of(R"(()[]<>=:#"%-+.,!?/*\)")},
      },
              i)) {
        string message = "Tokenizer didn't recognize ";
        message += m_last;
        throw std::runtime_error(message);
      }
      i++;
    }
  }

  explicit Tokenizer(std::istream& in) : m_in(in), m_last(next()) {}

private:
  auto next() -> char {
    m_in.get(m_last);
    return m_last;
  }

  [[nodiscard]] auto is_characteristic(const char_fn& fun, std::stringstream& stream) const -> bool {
    return fun(m_last, 0, stream);
  }

  auto select_characteristic(std::initializer_list<Characteristic> list, int i) -> bool {
    return any_of(list, [&](const auto& c) {
      auto stream = std::stringstream{};
      if (is_characteristic(c.m_fn, stream)) {
        m_tokens.emplace_back(c.m_type, consume_characteristic(c.m_fn, stream), i);
        return true;
      }
      return false;
    });
  }

  auto consume_characteristic(const char_fn& fun, std::stringstream& out) -> Atom {
    int i = 1;
    next();
    while (fun(m_last, i, out) && !m_in.eof()) {
      i++;
      next();
    }

    return make_atom(out.str());
  };
};

auto tokenize_preserve_skipped(std::istream& in) -> vector<Token> {
  auto tokenizer = Tokenizer(in);
  tokenizer.tokenize();
  return tokenizer.m_tokens;
}

auto tokenize(std::istream& in) -> vector<Token> {
  vector<Token> original = tokenize_preserve_skipped(in);
  vector<Token> filtered{};
  filtered.reserve(original.size());
  std::copy_if(original.begin(), original.end(), std::back_inserter(filtered),
               [](const Token& t) { return t.m_type != Token::Type::Skip; });
  return filtered;
}

auto operator<<(std::ostream& os, const Token& token) -> std::ostream& {
  os << "Token(" << Token::type_name(token.m_type);
  if (token.m_payload.has_value()) {
    os << ", \"";
    for (const char p : string(*token.m_payload)) {
      int c = (unsigned char)p;

      switch (c) {
      case '\\': os << "\\\\"; break;
      case '\"': os << "\\\""; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default:
        if (std::isprint(c) != 0) {
          os.put(p);
        } else {
          os << "\\x" << std::hex << c << std::dec;
        }
        break;
      }
    }
    os << "\", " << token.m_i;
    // os << ", " << static_cast<const void*>(*token.m_payload);
  }
  os << ")";
  return os;
}
} // namespace yume
