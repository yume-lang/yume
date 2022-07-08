#include "ast.hpp"

#include "atom.hpp"
#include "diagnostic/errors.hpp"
#include "diagnostic/source_location.hpp"
#include "qualifier.hpp"
#include "token.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <llvm/Support/raw_ostream.h>
#include <memory>

namespace yume::ast {

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

void AST::unify_val_ty() const {
  for (const auto* other : m_attach->depends) {
    if (m_val_ty == other->m_val_ty || other->m_val_ty == nullptr)
      return;

    if (m_val_ty == nullptr) {
      m_val_ty = other->m_val_ty;
    } else {
      const auto* merged = m_val_ty->coalesce(*other->m_val_ty);
      if (merged == nullptr) {
        throw std::logic_error("Conflicting types between AST nodes that are attached: `"s + m_val_ty->name() +
                               "` vs `" + other->m_val_ty->name() + "`!");
      }
      m_val_ty = merged;
    }
  }
}

namespace {
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
  }

  static auto unary_operators() {
    const static vector UNARY_OPERATORS = {
        SYM_MINUS,
        SYM_PLUS,
        SYM_BANG,
    };
    return UNARY_OPERATORS;
  }

  static auto to_string(Token token) -> string {
    string str{};
    llvm::raw_string_ostream(str) << token;
    return str;
  }

  /// Ignore any `Separator` tokens if any are present.
  /// \returns true if a separator was encountered (and consumed)
  auto ignore_separator([[maybe_unused]] const source_location location = source_location::current()) -> bool {
    bool found_separator = false;
    while (!tokens.at_end() && tokens->type == Separator) {
#ifdef YUME_SPEW_CONSUMED_TOKENS
      llvm::errs() << "consumed " << *tokens << " at " << at(location) << "\n";
#endif
      ++tokens;
      found_separator = true;
    }
    return found_separator;
  }

  /// If the next token doesn't have the type, `token_type`, throw a runtime exception.
  void expect(Token::Type token_type, const source_location location = source_location::current()) const {
    if (tokens.at_end())
      throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got the end of the file");

    if (tokens->type != token_type)
      throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got " + to_string(*tokens) +
                               " at " + at(location));
  }

  /// Consume all subsequent `Separator` tokens. Throws if none were found.
  void require_separator(const source_location location = source_location::current()) {
    if (!tokens.at_end())
      expect(Separator, location);
    ignore_separator(location);
  }

  /// Consume a token of the given type and payload. Throws if it wasn't encountered.
  void consume(TokenAtom token_atom, const source_location location = source_location::current()) {
    auto [token_type, payload] = token_atom;
    ignore_separator();
    expect(token_type, location);
    if (tokens->payload != payload) {
      throw std::runtime_error("Expected payload atom "s + string(payload) + ", got " + to_string(*tokens) + " at " +
                               at(location));
    }

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "consume: " << *tokens << " at " << at(location) << "\n";
#endif

    tokens++;
  }

  /// Attempt to consume a token of the given type and payload. Returns false if it wasn't encountered.
  auto try_consume(TokenAtom token_atom, [[maybe_unused]] const source_location location = source_location::current())
      -> bool {
    auto [token_type, payload] = token_atom;
    if (tokens.at_end() || tokens->type != token_type || tokens->payload != payload) {
      return false;
    }

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_consume: " << *tokens << " at " << at(location) << "\n";
#endif

    tokens++;
    return true;
  }

  /// Check if the token ahead by `ahead` is of the given type and payload.
  [[nodiscard]] auto try_peek(int ahead, TokenAtom token_atom,
                              [[maybe_unused]] const source_location location = source_location::current()) const
      -> bool {
    auto [token_type, payload] = token_atom;
    if (tokens.at_end())
      return false;

    auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << " "
                 << string(payload) << ", got " << *token << " at " << at(location) << "\n";
#endif

    return !(token->type != token_type || token->payload != payload);
  }

  /// Check if the token ahead by `ahead` is of type `token_type`.
  [[nodiscard]] auto try_peek(int ahead, Token::Type token_type,
                              [[maybe_unused]] const source_location location = source_location::current()) const
      -> bool {
    if (tokens.at_end())
      return false;
    auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << ", got " << *token
                 << " at " << at(location) << "\n";
#endif

    return token->type == token_type;
  }

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
  auto next([[maybe_unused]] const source_location location = source_location::current()) -> Token {
    auto tok = *tokens++;
#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "next: " << tok << " at " << at(location) << "\n";
#endif
    return tok;
  }

  /// Return the payload of the next token. Throws if the next token isn't a `Word`.
  auto consume_word(const source_location location = source_location::current()) -> string {
    ignore_separator();
    if (tokens->type != Word)
      throw std::runtime_error("Expected word, got the end of the file");
    if (tokens->type != Word)
      throw std::runtime_error("Expected word, got "s + to_string(*tokens) + " at " + at(location));

    return string(*next(location).payload);
  }

  /// Check if the string begins with a capital letter. Used for types, as all types must be capitalized.
  static auto is_uword(string_view word) -> bool { return isupper(word.front()) != 0; }

  /// Check if the ahead by `ahead` is a capitalized word.
  [[nodiscard]] auto try_peek_uword(int ahead,
                                    [[maybe_unused]] const source_location location = source_location::current()) const
      -> bool {
    auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_peek ahead by " << ahead << ": expected uword, got " << *token << " at " << at(location)
                 << "\n";
#endif

    return token->type == Word && is_uword(token->payload.value());
  }

  auto parse_stmt() -> unique_ptr<Stmt>;
  auto parse_expr() -> unique_ptr<Expr>;
  auto parse_type(bool implicit_self = false) -> unique_ptr<Type>;
  auto parse_type_name() -> unique_ptr<TypeName>;
  auto try_parse_type() -> optional<unique_ptr<Type>>;

  auto parse_fn_name() -> string {
    string name{};
    if (tokens->type == Word) {
      name = consume_word();
    } else if (tokens->type == Symbol) {
      // Try to parse an operator name, as in `def +()`
      bool found_op = false;
      for (const auto& op_row : operators()) {
        for (const auto& op : op_row) {
          if (try_consume(op)) {
            found_op = true;
            name = std::get<Atom>(op);
            break;
          }
        }
        if (found_op)
          break;
      }

      // If an operator wasn't found, try parse the operator []
      if (try_consume(SYM_LBRACKET)) {
        consume(SYM_RBRACKET);
        name = "[]";
      } else if (try_consume(SYM_BANG)) {
        name = "!"; // ! is unary, but the above operator check only checked binary ones
      }
    }

    // Check if an equal sign follows, for fused assignement operators such as `+=` or `[]=`
    if (try_consume(SYM_EQ))
      name += "=";

    return name;
  }

  auto parse_struct_decl() -> unique_ptr<StructDecl> {
    auto entry = tokens.begin();

    consume(KWD_STRUCT);
    const string name = consume_word();
    if (!is_uword(name))
      throw std::runtime_error("Expected capitalized name for struct decl");

    auto type_args = vector<string>{};
    if (try_consume(SYM_LBRACE))
      consume_with_commas_until(SYM_RBRACE, [&] { type_args.push_back(consume_word()); });

    consume(SYM_LPAREN);
    auto fields = vector<TypeName>{};
    consume_with_commas_until(SYM_RPAREN, [&] { fields.push_back(move(*parse_type_name())); });

    auto body = vector<unique_ptr<Stmt>>{};
    auto body_begin = entry;

    require_separator();

    body_begin = tokens.begin();
    while (!try_consume(KWD_END)) {
      body.push_back(parse_stmt());
      ignore_separator();
    }

    return ast_ptr<StructDecl>(entry, name, move(fields), type_args, make_ast<Compound>(body_begin, move(body)));
  }

  auto parse_fn_decl() -> unique_ptr<FnDecl> {
    auto entry = tokens.begin();

    consume(KWD_DEF);
    const string name = parse_fn_name();
    auto type_args = vector<string>{};
    if (try_consume(SYM_LBRACE))
      consume_with_commas_until(SYM_RBRACE, [&] { type_args.push_back(consume_word()); });

    consume(SYM_LPAREN);

    auto args = vector<TypeName>{};
    consume_with_commas_until(SYM_RPAREN, [&] { args.push_back(move(*parse_type_name())); });

    auto ret_type = try_parse_type();
    auto body = vector<unique_ptr<Stmt>>{};
    auto body_begin = entry;

    if (try_consume(SYM_EQ)) { // A "short" function definition, consists of a single expression
      if (try_consume(KWD_PRIMITIVE)) {
        consume(SYM_LPAREN);
        auto primitive = consume_word();
        consume(SYM_RPAREN);
        auto varargs = try_consume(KWD_VARARGS);
        return ast_ptr<FnDecl>(entry, name, move(args), type_args, move(ret_type), varargs, primitive);
      }
      body_begin = tokens.begin();
      auto expr = parse_expr();
      body.push_back(ast_ptr<ReturnStmt>(entry, move(expr)));
    } else {
      require_separator();

      body_begin = tokens.begin();
      while (!try_consume(KWD_END)) {
        body.push_back(parse_stmt());
        ignore_separator();
      }
    }

    return ast_ptr<FnDecl>(entry, name, move(args), type_args, move(ret_type),
                           make_ast<Compound>(body_begin, move(body)));
  }

  auto parse_var_decl() -> unique_ptr<VarDecl> {
    auto entry = tokens.begin();

    consume(KWD_LET);
    const string name = consume_word();
    auto type = try_parse_type();

    consume(SYM_EQ);

    auto init = parse_expr();

    return ast_ptr<VarDecl>(entry, name, move(type), move(init));
  }

  auto parse_while_stmt() -> unique_ptr<WhileStmt> {
    auto entry = tokens.begin();

    consume(KWD_WHILE);
    auto cond = parse_expr();

    ignore_separator();

    auto body_begin = tokens.begin();
    auto body = vector<unique_ptr<Stmt>>{};
    while (!try_consume(KWD_END)) {
      body.push_back(parse_stmt());
      ignore_separator();
    }

    auto compound = make_ast<Compound>(body_begin, move(body));

    return ast_ptr<WhileStmt>(entry, move(cond), move(compound));
  }

  auto parse_return_stmt() -> unique_ptr<ReturnStmt> {
    auto entry = tokens.begin();

    consume(KWD_RETURN);
    if (!tokens.at_end() && try_peek(0, Separator))
      return ast_ptr<ReturnStmt>(entry, optional<unique_ptr<Expr>>{});

    auto expr = parse_expr();

    return ast_ptr<ReturnStmt>(entry, optional<unique_ptr<Expr>>{move(expr)});
  }

  auto parse_if_stmt() -> unique_ptr<IfStmt> {
    auto entry = tokens.begin();
    auto clause_begin = entry;
    consume(KWD_IF);
    auto cond = parse_expr();
    if (!try_consume(KWD_THEN))
      require_separator();

    auto current_entry = tokens.begin();
    auto else_entry = tokens.begin();
    auto clauses = vector<IfClause>{};
    auto current_body = vector<unique_ptr<Stmt>>{};
    auto else_body = vector<unique_ptr<Stmt>>{};
    bool in_else = false;

    while (true) {
      auto current_clause_begin = tokens.begin();
      if (try_consume(KWD_END))
        break;
      if (try_consume(KWD_ELSE)) {
        // An `else` followed by an `if` begins a new clause of the same if statement.
        if (!in_else && try_consume(KWD_IF)) {
          clauses.emplace_back(ts(clause_begin), move(cond), make_ast<Compound>(current_entry, move(current_body)));
          current_body = vector<unique_ptr<Stmt>>{};
          cond = parse_expr();
          current_entry = tokens.begin();
          clause_begin = current_clause_begin;
        } else {
          in_else = true;
          else_entry = tokens.begin();
        }
        if (!try_consume(KWD_THEN))
          require_separator();
      }
      auto st = parse_stmt();
      if (in_else) {
        else_body.push_back(move(st));
      } else {
        current_body.push_back(move(st));
      }
    }

    if (else_body.empty())
      else_entry = tokens.begin();

    clauses.emplace_back(ts(clause_begin, else_entry - 1), move(cond),
                         Compound(ts(current_entry, else_entry - 1), move(current_body)));

    auto else_clause = optional<Compound>{};
    if (!else_body.empty())
      else_clause.emplace(ts(else_entry), move(else_body));

    return ast_ptr<IfStmt>(entry, move(clauses), move(else_clause));
  }

  auto parse_number_expr() -> unique_ptr<NumberExpr> {
    auto entry = tokens.begin();
    expect(Number);
    auto value = stoll(string(*next().payload));

    return ast_ptr<NumberExpr>({entry, 1}, value);
  }

  auto parse_string_expr() -> unique_ptr<StringExpr> {
    auto entry = tokens.begin();
    expect(Token::Type::Literal);
    auto value = string(*next().payload);

    return ast_ptr<StringExpr>({entry, 1}, value);
  }

  auto parse_char_expr() -> unique_ptr<CharExpr> {
    auto entry = tokens.begin();
    expect(Token::Type::Char);
    auto value = string(*next().payload)[0];

    return ast_ptr<CharExpr>({entry, 1}, value);
  }

  auto parse_primary() -> unique_ptr<Expr> {
    const auto guard = make_guard("Parsing primary expression");

    auto entry = tokens.begin();
    if (try_consume(SYM_LPAREN)) {
      auto val = parse_expr();
      consume(SYM_RPAREN);
      return val;
    }

    if (tokens->type == Number)
      return parse_number_expr();
    if (tokens->type == Token::Type::Literal)
      return parse_string_expr();
    if (tokens->type == Token::Type::Char)
      return parse_char_expr();
    if (try_consume(KWD_TRUE))
      return ast_ptr<BoolExpr>(entry, true);
    if (try_consume(KWD_FALSE))
      return ast_ptr<BoolExpr>(entry, false);

    if (tokens->type == Word) {
      if (try_peek_uword(0)) {
        auto type = parse_type();
        if (try_consume(SYM_LPAREN)) {
          auto call_args = vector<unique_ptr<Expr>>{};
          consume_with_commas_until(SYM_RPAREN, [&] { call_args.push_back(parse_expr()); });
          return ast_ptr<CtorExpr>(entry, move(type), move(call_args));
        }
        if (try_consume(SYM_COLON)) {
          consume(SYM_LBRACKET);
          auto slice_members = vector<unique_ptr<Expr>>{};
          consume_with_commas_until(SYM_RBRACKET, [&] { slice_members.push_back(parse_expr()); });
          return ast_ptr<SliceExpr>(entry, move(type), move(slice_members));
        }

        throw std::runtime_error("Couldn't make an expression from here with a type");
      }
      auto name = consume_word();
      if (try_consume(SYM_LPAREN)) {
        auto call_args = vector<unique_ptr<Expr>>{};
        consume_with_commas_until(SYM_RPAREN, [&] { call_args.push_back(parse_expr()); });
        return ast_ptr<CallExpr>(entry, name, move(call_args));
      }
      return ast_ptr<VarExpr>({entry, 1}, name);
    }
    throw std::runtime_error("Couldn't make an expression from here");
  }

  auto parse_receiver(unique_ptr<Expr> receiver, auto receiver_entry) -> unique_ptr<Expr> {
    auto entry = tokens.begin();
    if (try_consume(SYM_DOT)) {
      auto name = consume_word();
      auto call_args = vector<unique_ptr<Expr>>{};
      call_args.push_back(move(receiver));
      if (try_consume(SYM_LPAREN)) { // A call with a dot `a.b(...)`
        consume_with_commas_until(SYM_RPAREN, [&] { call_args.push_back(parse_expr()); });
        auto call = ast_ptr<CallExpr>(entry + 1, name, move(call_args));
        return parse_receiver(move(call), receiver_entry);
      }
      if (try_consume(SYM_EQ)) { // A setter `a.b = ...`
        auto value = parse_expr();
        call_args.push_back(move(value));
        auto call = ast_ptr<CallExpr>(entry + 1, name + '=', move(call_args));
        return parse_receiver(move(call), receiver_entry);
      }
      auto noarg_call = ast_ptr<CallExpr>(receiver_entry, name, move(call_args));
      return parse_receiver(move(noarg_call), receiver_entry);
    }
    if (try_consume(SYM_EQ)) {
      auto value = parse_expr();
      auto assign = ast_ptr<AssignExpr>(receiver_entry, move(receiver), move(value));
      return parse_receiver(move(assign), receiver_entry);
    }
    if (try_consume(SYM_LBRACKET)) {
      auto args = vector<unique_ptr<Expr>>{};
      args.push_back(move(receiver));
      args.push_back(parse_expr());
      consume(SYM_RBRACKET);
      if (try_consume(SYM_EQ)) {
        auto value = parse_expr();
        args.push_back(move(value));
        auto call = ast_ptr<CallExpr>(entry, "[]=", move(args));
        return parse_receiver(move(call), receiver_entry);
      }
      auto call = ast_ptr<CallExpr>(entry, "[]", move(args));
      return parse_receiver(move(call), receiver_entry);
    }
    if (try_consume(SYM_COLON)) {
      consume(SYM_COLON);
      auto field = consume_word();
      auto access = ast_ptr<FieldAccessExpr>(receiver_entry, move(receiver), field);
      return parse_receiver(move(access), receiver_entry);
    }
    return receiver;
  }

  auto parse_receiver() -> unique_ptr<Expr> {
    auto entry = tokens.begin();
    return parse_receiver(parse_primary(), entry);
  }

  auto parse_unary() -> unique_ptr<Expr> {
    auto entry = tokens.begin();
    for (const auto& un_op : unary_operators()) {
      if (try_consume(un_op)) {
        auto value = parse_receiver();
        auto args = vector<unique_ptr<Expr>>{};
        args.push_back(move(value));
        return ast_ptr<CallExpr>(entry, string(std::get<Atom>(un_op)), move(args));
      }
    }
    return parse_receiver();
  }

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
} // namespace

auto Parser::parse_stmt() -> unique_ptr<Stmt> {
  auto stat = unique_ptr<Stmt>();

  if (tokens->is_a(KWD_DEF))
    stat = parse_fn_decl();
  else if (tokens->is_a(KWD_STRUCT))
    stat = parse_struct_decl();
  else if (tokens->is_a(KWD_LET))
    stat = parse_var_decl();
  else if (tokens->is_a(KWD_WHILE))
    stat = parse_while_stmt();
  else if (tokens->is_a(KWD_IF))
    stat = parse_if_stmt();
  else if (tokens->is_a(KWD_RETURN))
    stat = parse_return_stmt();
  else
    stat = parse_expr();

  require_separator();
  return stat;
}

auto Parser::parse_type(bool implicit_self) -> unique_ptr<Type> {
  auto entry = tokens.begin();
  auto base = [&]() -> unique_ptr<Type> {
    if (implicit_self || try_consume(KWD_SELF_TYPE))
      return ast_ptr<SelfType>(entry);

    const string name = consume_word();
    if (!is_uword(name))
      throw std::runtime_error("Expected capitalized payload for simple type");

    return ast_ptr<SimpleType>(entry, name);
  }();
  while (true) {
    if (try_consume(KWD_PTR)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ptr);
    } else if (try_consume(KWD_MUT)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Mut);
    } else if (try_peek(0, SYM_LBRACKET) && try_peek(1, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(SYM_LBRACKET);
      consume(SYM_RBRACKET);
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Slice);
    } else if (try_consume(SYM_LBRACE)) {
      auto type_args = vector<unique_ptr<Type>>{};
      consume_with_commas_until(SYM_RBRACE, [&] { type_args.push_back(parse_type()); });

      base = ast_ptr<TemplatedType>(entry, move(base), move(type_args));
    } else {
      break;
    }
  }

  return base;
}

auto Parser::try_parse_type() -> optional<unique_ptr<Type>> {
  auto entry = tokens.begin();
  if (tokens->type != Word || !tokens->payload.has_value())
    return {};

  const string name = consume_word();
  if (make_atom(name) != std::get<Atom>(KWD_SELF_TYPE) && !is_uword(name))
    return {};

  unique_ptr<Type> base{};
  if (make_atom(name) == std::get<Atom>(KWD_SELF_TYPE))
    base = ast_ptr<SelfType>(entry);
  else
    base = ast_ptr<SimpleType>(entry, name);

  while (true) {
    if (try_consume(KWD_PTR)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ptr);
    } else if (try_consume(KWD_MUT)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Mut);
    } else if (try_peek(0, SYM_LBRACKET) && try_peek(1, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(SYM_LBRACKET);
      consume(SYM_RBRACKET);
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Slice);
    } else if (try_consume(SYM_LBRACE)) {
      auto type_args = vector<unique_ptr<Type>>{};
      consume_with_commas_until(SYM_RBRACE, [&] { type_args.push_back(parse_type()); });

      base = ast_ptr<TemplatedType>(entry, move(base), std::move(type_args));
    } else {
      break;
    }
  }

  return base;
}

auto Parser::parse_type_name() -> unique_ptr<TypeName> {
  auto entry = tokens.begin();
  if (try_consume(KWD_SELF_ITEM)) {
    unique_ptr<Type> type = parse_type(/* implicit_self= */ true);
    return ast_ptr<TypeName>(entry, move(type), "self");
  }
  const string name = consume_word();
  unique_ptr<Type> type = parse_type();
  return ast_ptr<TypeName>(entry, move(type), name);
}

auto Parser::parse_expr() -> unique_ptr<Expr> { return parse_operator(); }

auto Program::parse(TokenIterator& tokens) -> unique_ptr<Program> {
  auto parser = Parser{tokens};
  parser.ignore_separator();
  auto entry = tokens.begin();

  auto statements = vector<unique_ptr<Stmt>>{};
  while (!tokens.at_end()) {
    statements.push_back(parser.parse_stmt());
  }

  return make_unique<Program>(parser.ts(entry), move(statements));
}
} // namespace yume::ast
