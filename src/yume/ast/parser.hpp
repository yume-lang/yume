#pragma once

#include "ast/ast.hpp"
#include "atom.hpp"
#include "diagnostic/errors.hpp"
#include "diagnostic/source_location.hpp"
#include "token.hpp"
#include "util.hpp"
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace yume::ast {
using VectorTokenIterator = vector<Token>::iterator;

/// An iterator-like holding `Token`s, used when parsing.
/**
 * Every parse method usually takes this as the first parameter. This is its own struct as it actually holds two
 * iterators, where one is the end. This is to provide safe indexing (avoiding going past the end) without having to
 * pass the end iterator around as a separate parameter.
 */
class TokenIterator {
  VectorTokenIterator m_iterator;
  VectorTokenIterator m_end;

public:
  constexpr TokenIterator(const VectorTokenIterator& iterator, const VectorTokenIterator& end)
      : m_iterator{iterator}, m_end{end} {}

  /// Check if the iterator is at the end and no more `Token`s could possibly be read.
  [[nodiscard]] constexpr auto at_end() const noexcept -> bool { return m_iterator == m_end; }
  [[nodiscard]] auto constexpr operator->() const -> Token* {
    if (at_end())
      throw std::runtime_error("Can't dereference at end");
    return m_iterator.operator->();
  }
  [[nodiscard]] constexpr auto operator*() const -> Token {
    if (at_end())
      throw std::runtime_error("Can't dereference at end");
    return m_iterator.operator*();
  }
  [[nodiscard]] constexpr auto operator+(int i) const noexcept -> TokenIterator {
    return TokenIterator{m_iterator + i, m_end};
  }
  constexpr auto operator++() -> TokenIterator& {
    if (at_end())
      throw std::runtime_error("Can't increment past end");

    ++m_iterator;
    return *this;
  }
  auto operator++(int) -> TokenIterator {
    if (at_end())
      throw std::runtime_error("Can't increment past end");

    return TokenIterator{m_iterator++, m_end};
  }
  /// Returns the underlying iterator. This shouldn't really be needed but I'm too lazy to properly model an iterator.
  [[nodiscard]] auto begin() const -> VectorTokenIterator { return m_iterator; }
};
} // namespace yume::ast

namespace yume::ast::parser {
using TokenAtom = std::pair<Token::Type, Atom>;

static const TokenAtom KWD_IF = {Token::Type::Word, "if"_a};
static const TokenAtom KWD_DEF = {Token::Type::Word, "def"_a};
static const TokenAtom KWD_END = {Token::Type::Word, "end"_a};
static const TokenAtom KWD_LET = {Token::Type::Word, "let"_a};
static const TokenAtom KWD_PTR = {Token::Type::Word, "ptr"_a};
static const TokenAtom KWD_MUT = {Token::Type::Word, "mut"_a};
static const TokenAtom KWD_ELSE = {Token::Type::Word, "else"_a};
static const TokenAtom KWD_SELF_ITEM = {Token::Type::Word, "self"_a};
static const TokenAtom KWD_SELF_TYPE = {Token::Type::Word, "Self"_a};
static const TokenAtom KWD_THEN = {Token::Type::Word, "then"_a};
static const TokenAtom KWD_TRUE = {Token::Type::Word, "true"_a};
static const TokenAtom KWD_FALSE = {Token::Type::Word, "false"_a};
static const TokenAtom KWD_WHILE = {Token::Type::Word, "while"_a};
static const TokenAtom KWD_STRUCT = {Token::Type::Word, "struct"_a};
static const TokenAtom KWD_RETURN = {Token::Type::Word, "return"_a};
static const TokenAtom KWD_VARARGS = {Token::Type::Word, "__varargs__"_a};
static const TokenAtom KWD_PRIMITIVE = {Token::Type::Word, "__primitive__"_a};

static const TokenAtom SYM_COMMA = {Token::Type::Symbol, ","_a};
static const TokenAtom SYM_DOT = {Token::Type::Symbol, "."_a};
static const TokenAtom SYM_EQ = {Token::Type::Symbol, "="_a};
static const TokenAtom SYM_LPAREN = {Token::Type::Symbol, "("_a};
static const TokenAtom SYM_RPAREN = {Token::Type::Symbol, ")"_a};
static const TokenAtom SYM_LBRACKET = {Token::Type::Symbol, "["_a};
static const TokenAtom SYM_RBRACKET = {Token::Type::Symbol, "]"_a};
static const TokenAtom SYM_LBRACE = {Token::Type::Symbol, "{"_a};
static const TokenAtom SYM_RBRACE = {Token::Type::Symbol, "}"_a};
static const TokenAtom SYM_EQ_EQ = {Token::Type::Symbol, "=="_a};
static const TokenAtom SYM_NEQ = {Token::Type::Symbol, "!="_a};
static const TokenAtom SYM_LT = {Token::Type::Symbol, "<"_a};
static const TokenAtom SYM_GT = {Token::Type::Symbol, ">"_a};
static const TokenAtom SYM_PLUS = {Token::Type::Symbol, "+"_a};
static const TokenAtom SYM_MINUS = {Token::Type::Symbol, "-"_a};
static const TokenAtom SYM_PERCENT = {Token::Type::Symbol, "%"_a};
static const TokenAtom SYM_SLASH_SLASH = {Token::Type::Symbol, "//"_a};
static const TokenAtom SYM_STAR = {Token::Type::Symbol, "*"_a};
static const TokenAtom SYM_BANG = {Token::Type::Symbol, "!"_a};
static const TokenAtom SYM_COLON = {Token::Type::Symbol, ":"_a};
static const TokenAtom SYM_COLON_COLON = {Token::Type::Symbol, "::"_a};

class TokenRange {
  span<Token> m_span;

public:
  constexpr TokenRange(auto&& begin, int end) : m_span{begin.base(), static_cast<size_t>(end)} {}
  constexpr TokenRange(auto&& begin, auto&& end) : m_span{begin.base(), end.base()} {}

  /* implicit */ operator span<Token>() const { return m_span; }
};

struct Parser {
  TokenIterator& tokens;

  template <typename T, typename U> static auto ts(T&& begin, U&& end) -> span<Token> {
    return TokenRange{std::forward<T>(begin), std::forward<U>(end)};
  }

  [[nodiscard]] auto ts(const VectorTokenIterator& entry) const -> span<Token> {
    return span<Token>{entry.base(), tokens.begin().base()};
  }

  template <typename T, typename... Args> auto ast_ptr(const VectorTokenIterator& entry, Args&&... args) {
    return std::make_unique<T>(span<Token>{entry.base(), tokens.begin().base()}, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args> auto ast_ptr(TokenRange&& range, Args&&... args) {
    return std::make_unique<T>(static_cast<span<Token>>(range), std::forward<Args>(args)...);
  }

  template <typename T, typename... Args> auto make_ast(const VectorTokenIterator& entry, Args&&... args) {
    return T(span<Token>{entry.base(), tokens.begin().base()}, std::forward<Args>(args)...);
  }

  constexpr static auto Symbol = Token::Type::Symbol;
  constexpr static auto Word = Token::Type::Word;
  constexpr static auto Separator = Token::Type::Separator;
  constexpr static auto Number = Token::Type::Number;

  [[nodiscard]] auto make_guard(const string& message) const -> ParserStackTrace { return {message, *tokens}; }

  static auto operators() {
    const static array OPERATORS = {
        vector{SYM_EQ_EQ, SYM_NEQ, SYM_GT, SYM_LT},
        vector{SYM_PLUS, SYM_MINUS},
        vector{SYM_PERCENT, SYM_SLASH_SLASH, SYM_STAR},
    };
    return OPERATORS;
  };

  static auto unary_operators() {
    const static vector UNARY_OPERATORS = {
        SYM_MINUS,
        SYM_PLUS,
        SYM_BANG,
    };
    return UNARY_OPERATORS;
  };

  static auto to_string(Token token) -> string;

  /// Ignore any `Separator` tokens if any are present.
  /// \returns true if a separator was encountered (and consumed)
  auto ignore_separator(source_location location = source_location::current()) -> bool;

  /// If the next token doesn't have the type, `token_type`, throw a runtime exception.
  void expect(Token::Type token_type, source_location location = source_location::current()) const;

  /// Consume all subsequent `Separator` tokens. Throws if none were found.
  void require_separator(source_location location = source_location::current());

  /// Consume a token of the given type and payload. Throws if it wasn't encountered.
  void consume(TokenAtom token_atom, source_location location = source_location::current());

  /// Attempt to consume a token of the given type and payload. Returns false if it wasn't encountered.
  auto try_consume(TokenAtom token_atom, source_location location = source_location::current()) -> bool;

  /// Check if the token ahead by `ahead` is of the given type and payload.
  [[nodiscard]] auto try_peek(int ahead, TokenAtom token_atom,
                              source_location location = source_location::current()) const -> bool;

  /// Check if the token ahead by `ahead` is of type `token_type`.
  [[nodiscard]] auto try_peek(int ahead, Token::Type token_type,
                              source_location location = source_location::current()) const -> bool;

  /// Consume tokens until a token of the given type and payload is encountered.
  /// `action` (a no-arg function) is called every time. Between each call, a comma is expected.
  template <typename T>
  requires std::invocable<T>
  void consume_with_commas_until(TokenAtom token_atom, T action,
                                 const source_location location = source_location::current()) {
    int i = 0;
    while (!try_consume(token_atom, location)) {
      if (i++ > 0)
        consume(SYM_COMMA, location);
      action();
    }
  }

  /// Return the next token and increment the iterator.
  auto next([[maybe_unused]] source_location location = source_location::current()) -> Token;

  /// Return the payload of the next token. Throws if the next token isn't a `Word`.
  auto consume_word(source_location location = source_location::current()) -> string;

  /// Check if the string begins with a capital letter. Used for types, as all types must be capitalized.
  static auto is_uword(string_view word) -> bool { return isupper(word.front()) != 0; }

  /// Check if the ahead by `ahead` is a capitalized word.
  [[nodiscard]] auto try_peek_uword(int ahead, source_location location = source_location::current()) const -> bool;

  auto parse_stmt() -> unique_ptr<Stmt>;
  auto parse_expr() -> unique_ptr<Expr>;

  auto parse_type(bool implicit_self = false) -> unique_ptr<Type>;

  auto parse_type_name() -> unique_ptr<TypeName>;

  auto try_parse_type() -> optional<unique_ptr<Type>>;

  auto parse_fn_name() -> string;

  auto parse_struct_decl() -> unique_ptr<StructDecl>;

  auto parse_fn_or_ctor_decl() -> unique_ptr<Stmt>;
  auto parse_fn_decl() -> unique_ptr<FnDecl>;
  auto parse_ctor_decl() -> unique_ptr<CtorDecl>;
  auto parse_type_name_or_ctor_field() -> variant<TypeName, VarExpr>;

  auto parse_var_decl() -> unique_ptr<VarDecl>;

  auto parse_while_stmt() -> unique_ptr<WhileStmt>;

  auto parse_return_stmt() -> unique_ptr<ReturnStmt>;

  auto parse_if_stmt() -> unique_ptr<IfStmt>;

  auto parse_number_expr() -> unique_ptr<NumberExpr>;

  auto parse_string_expr() -> unique_ptr<StringExpr>;

  auto parse_char_expr() -> unique_ptr<CharExpr>;

  auto parse_primary() -> unique_ptr<Expr>;

  auto parse_receiver(unique_ptr<Expr> receiver, VectorTokenIterator receiver_entry) -> unique_ptr<Expr>;

  auto parse_receiver() -> unique_ptr<Expr>;

  auto parse_unary() -> unique_ptr<Expr>;

  template <size_t N = 0> auto parse_operator() -> unique_ptr<Expr> {
    auto entry = tokens.begin();
    const auto ops = operators();
    if constexpr (N == ops.size()) {
      return parse_unary();
    } else {
      auto left = parse_operator<N + 1>();
      while (true) {
        auto found_operator = false;
        for (const auto& op : ops[N]) {
          if (try_consume(op)) {
            auto right = parse_operator<N + 1>();
            auto args = vector<unique_ptr<Expr>>{};
            args.push_back(move(left));
            args.push_back(move(right));
            left = ast_ptr<CallExpr>(entry, string(std::get<Atom>(op)), move(args));
            found_operator = true;
            break;
          }
        }
        if (!found_operator)
          break;
      }
      return left;
    }
  }
};
} // namespace yume::ast::parser
