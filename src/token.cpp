//
// Created by rymiel on 5/8/22.
//

#include "token.hpp"
#include <algorithm>
#include <iostream>
#include <ranges>
#include <utility>
#include <vector>

namespace yume {
using char_raw_fn = int(int);
using char_fn = std::function<int(int, int)>;

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

  Characteristic(Token::Type type, const std::function<int(int)>& fn)
      : m_fn([fn](int c, int i) { return fn(c); }), m_type(type) {}
  Characteristic(Token::Type type, char_fn fn) : m_fn(std::move(fn)), m_type(type) {}
  Characteristic(Token::Type type, char_raw_fn fn) : m_fn([fn](int c, int i) { return fn(c); }), m_type(type) {}
  Characteristic(Token::Type type, char c) : m_fn([check = c](char c, int i) { return c == check; }), m_type(type) {}
  Characteristic(Token::Type type, const char* cs)
      : m_fn([checks = string(cs)](char c, int i) { return i == 0 && checks.find(c) != string::npos; }), m_type(type) {}
  Characteristic(Token::Type type, string s) : m_fn([s = move(s)](char c, int i) { return s[i] == c; }), m_type(type) {}
};

struct Tokenizer {
  vector<Token> m_tokens{};
  std::istream& m_in;
  char m_last;

  void tokenize() {
    using namespace std::literals::string_literals;
    auto isword = [](int c, int i) { return (i == 0 && std::isalpha(c) != 0) || std::isalnum(c) != 0 || c == '_'; };
    auto isstr = [end = false](int c, int i) mutable {
      if (i == 0) {
        return c == '"';
      }
      if (end) {
        return false;
      }
      if (c == '"' && !end) {
        end = true;
      }
      return true;
    };
    auto iscomment = [](int c, int i) { return (i == 0 && c == '#') || (i > 0 && c != '\n'); };

    int i = 0;
    while (!m_in.eof()) {
      if (!selectCharacteristic(
              {
                  {Token::Type::Separator, '\n'                     },
                  {Token::Type::Skip,      std::isspace             },
                  {Token::Type::Skip,      iscomment                },
                  {Token::Type::Number,    std::isdigit             },
                  {Token::Type::Literal,   isstr                    },
                  {Token::Type::Word,      isword                   },
                  {Token::Type::Symbol,    "=="s                    },
                  {Token::Type::Symbol,    "//"s                    },
                  {Token::Type::Symbol,    R"(()[]<>=:#"%-+.,!?/*\)"},
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

  auto swap() -> char {
    auto c = m_last;
    next();
    return c;
  }

  [[nodiscard]] auto isCharacteristic(const char_fn& fun) const -> bool { return fun(m_last, 0) != 0; }

  auto selectCharacteristic(std::initializer_list<Characteristic> list, int i) -> bool {
    return any_of(list, [&](const auto& c) {
      if (isCharacteristic(c.m_fn)) {
        m_tokens.emplace_back(c.m_type, consumeCharacteristic(c.m_fn), i);
        return true;
      }
      return false;
    });
  }

  auto consumeCharacteristic(const char_fn& fun) -> Atom {
    auto out = std::string{};
    int i = 0;
    while (fun(m_last, i) != 0 && !m_in.eof()) {
      out += swap();
      i++;
    }

    return make_atom(out);
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
