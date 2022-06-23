#include "ast.hpp"
#include "../diagnostic/source_location.hpp"
#include "../visitor.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <llvm/Support/raw_ostream.h>
#include <memory>

namespace yume::ast {

static auto ts(auto&& begin, int end) { return span<Token>(begin.base(), end); }
static auto ts(auto&& begin, TokenIterator& end) { return span<Token>(begin.base(), end.begin().base()); }
static auto ts(auto&& begin, auto&& end) { return span<Token>(begin.base(), end.base()); }

template <typename T, typename... Args> static auto make_ast(auto&& begin, auto&& end, Args&&... args) {
  return std::make_unique<T>(ts(begin, end), std::forward<Args>(args)...);
}

constexpr static auto Symbol = Token::Type::Symbol;
constexpr static auto Word = Token::Type::Word;
constexpr static auto Separator = Token::Type::Separator;
constexpr static auto Number = Token::Type::Number;

static const Atom KWD_IF = "if"_a;
static const Atom KWD_DEF = "def"_a;
static const Atom KWD_END = "end"_a;
static const Atom KWD_LET = "let"_a;
static const Atom KWD_PTR = "ptr"_a;
static const Atom KWD_MUT = "mut"_a;
static const Atom KWD_ELSE = "else"_a;
static const Atom KWD_SELF_ITEM = "self"_a;
static const Atom KWD_SELF_TYPE = "Self"_a;
static const Atom KWD_THEN = "then"_a;
static const Atom KWD_TRUE = "true"_a;
static const Atom KWD_FALSE = "false"_a;
static const Atom KWD_WHILE = "while"_a;
static const Atom KWD_STRUCT = "struct"_a;
static const Atom KWD_RETURN = "return"_a;
static const Atom KWD_VARARGS = "__varargs__"_a;
static const Atom KWD_PRIMITIVE = "__primitive__"_a;

static const Atom SYM_COMMA = ","_a;
static const Atom SYM_DOT = "."_a;
static const Atom SYM_EQ = "="_a;
static const Atom SYM_LPAREN = "("_a;
static const Atom SYM_RPAREN = ")"_a;
static const Atom SYM_LBRACKET = "["_a;
static const Atom SYM_RBRACKET = "]"_a;
static const Atom SYM_LBRACE = "{"_a;
static const Atom SYM_RBRACE = "}"_a;
static const Atom SYM_EQ_EQ = "=="_a;
static const Atom SYM_NEQ = "!="_a;
static const Atom SYM_GT = ">"_a;
static const Atom SYM_LT = "<"_a;
static const Atom SYM_PLUS = "+"_a;
static const Atom SYM_MINUS = "-"_a;
static const Atom SYM_PERCENT = "%"_a;
static const Atom SYM_SLASH_SLASH = "//"_a;
static const Atom SYM_STAR = "*"_a;
static const Atom SYM_BANG = "!"_a;
static const Atom SYM_COLON = ":"_a;

auto operators() {
  const static vector<vector<Atom>> OPERATORS = {
      {SYM_EQ_EQ, SYM_NEQ, SYM_GT, SYM_LT},
      {SYM_PLUS, SYM_MINUS},
      {SYM_PERCENT, SYM_SLASH_SLASH, SYM_STAR},
  };
  return OPERATORS;
}

auto unary_operators() {
  const static vector<Atom> UNARY_OPERATORS = {
      SYM_MINUS,
      SYM_PLUS,
      SYM_BANG,
  };
  return UNARY_OPERATORS;
}

auto to_string(Token token) -> string {
  string str{};
  llvm::raw_string_ostream(str) << token;
  return str;
}

/// Ignore any `Separator` tokens if any are present.
/// \returns true if a separator was encountered (and consumed)
auto ignore_separator(TokenIterator& tokens,
                      [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  bool found_separator = false;
  while (!tokens.at_end() && tokens->m_type == Separator) {
#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "consumed " << *tokens << " at " << at(location) << "\n";
#endif
    ++tokens;
    found_separator = true;
  }
  return found_separator;
}

/// If the next token doesn't have the type, `token_type`, throw a runtime exception.
void expect(TokenIterator& tokens, Token::Type token_type,
            const source_location location = source_location::current()) {
  if (tokens->m_type != token_type) {
    throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got " + to_string(*tokens) +
                             " at " + at(location));
  }
}

/// Consume all subsequent `Separator` tokens. Throws if none were found.
void require_separator(TokenIterator& tokens, const source_location location = source_location::current()) {
  expect(tokens, Separator, location);
  ignore_separator(tokens, location);
}

/// Consume a token of type `token_type` with the given `payload`. Throws if it wasn't encountered.
void consume(TokenIterator& tokens, Token::Type token_type, Atom payload,
             const source_location location = source_location::current()) {
  ignore_separator(tokens);
  expect(tokens, token_type, location);
  if (tokens->m_payload != payload) {
    throw std::runtime_error("Expected payload atom "s + string(payload) + ", got " + to_string(*tokens) + " at " +
                             at(location));
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  llvm::errs() << "consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
}

/// Attempt to consume a token of type `token_type` with the given `payload`. Does nothing if it wasn't encountered.
auto try_consume(TokenIterator& tokens, Token::Type tokenType, Atom payload,
                 [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  if (tokens->m_type != tokenType || tokens->m_payload != payload) {
    return false;
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  llvm::errs() << "try_consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
  return true;
}

/// Check if the token ahead by `ahead` is of type `token_type` with the given `payload`.
auto try_peek(TokenIterator& tokens, int ahead, Token::Type token_type, Atom payload,
              [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  llvm::errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << " "
               << string(payload) << ", got " << *token << " at " << at(location) << "\n";
#endif

  return !(token->m_type != token_type || token->m_payload != payload);
}

/// Check if the token ahead by `ahead` is of type `token_type`.
auto try_peek(TokenIterator& tokens, int ahead, Token::Type token_type,
              [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  llvm::errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << ", got " << *token
               << " at " << at(location) << "\n";
#endif

  return token->m_type == token_type;
}

/// Consume tokens until a token of type `token_type` with the given `payload` is encountered.
/// `action` (a no-arg function) is called every time. Between each call, a comma is expected.
void consume_with_commas_until(TokenIterator& tokens, Token::Type token_type, Atom payload, auto action,
                               const source_location location = source_location::current()) {
  int i = 0;
  while (!try_consume(tokens, token_type, payload, location)) {
    if (i++ > 0)
      consume(tokens, Symbol, SYM_COMMA, location);
    action();
  }
}

/// Return the next token and increment the iterator.
auto next(TokenIterator& tokens, [[maybe_unused]] const source_location location = source_location::current())
    -> Token {
  auto tok = *tokens++;
#ifdef YUME_SPEW_CONSUMED_TOKENS
  llvm::errs() << "next: " << tok << " at " << at(location) << "\n";
#endif
  return tok;
}

/// Return the payload of the next token. Throws if the next token isn't a `Word`.
auto consume_word(TokenIterator& tokens, const source_location location = source_location::current()) -> string {
  ignore_separator(tokens);
  if (tokens->m_type != Word)
    throw std::runtime_error("Expected word, got "s + to_string(*tokens) + " at " + at(location));

  return *next(tokens, location).m_payload;
}

/// Check if the string begins with a capital letter. Used for types, as all types must be capitalized.
auto is_uword(const string& word) -> bool { return isupper(word.front()) != 0; }

/// Check if the ahead by `ahead` is a capitalized word.
auto try_peek_uword(TokenIterator& tokens, int ahead,
                    [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  llvm::errs() << "try_peek ahead by " << ahead << ": expected uword, got " << *token << " at " << at(location) << "\n";
#endif

  return token->m_type == Word && is_uword(token->m_payload.value());
}

static auto parse_stmt(TokenIterator& tokens) -> unique_ptr<Stmt>;
static auto parse_expr(TokenIterator& tokens) -> unique_ptr<Expr>;
static auto parse_type(TokenIterator& tokens, bool implicit_self = false) -> unique_ptr<Type>;
static auto parse_type_name(TokenIterator& tokens) -> unique_ptr<TypeName>;
static auto try_parse_type(TokenIterator& tokens) -> optional<unique_ptr<Type>>;

auto Program::parse(TokenIterator& tokens) -> unique_ptr<Program> {
  ignore_separator(tokens);
  auto entry = tokens.begin();

  auto statements = vector<unique_ptr<Stmt>>{};
  while (!tokens.at_end()) {
    statements.push_back(parse_stmt(tokens));
  }

  return make_ast<Program>(entry, tokens, move(statements));
}

static auto parse_fn_name(TokenIterator& tokens) -> string {
  string name{};
  if (tokens->m_type == Word) {
    name = consume_word(tokens);
  } else if (tokens->m_type == Symbol) {
    // Try to parse an operator name, as in `def +()`
    bool found_op = false;
    for (const auto& op_row : operators()) {
      for (const auto& op : op_row) {
        if (try_consume(tokens, Symbol, op)) {
          found_op = true;
          name = op;
          break;
        }
      }
      if (found_op)
        break;
    }

    // If an operator wasn't found, try parse the operator []
    if (try_consume(tokens, Symbol, SYM_LBRACKET)) {
      consume(tokens, Symbol, SYM_RBRACKET);
      name = "[]";
    } else if (try_consume(tokens, Symbol, SYM_BANG)) {
      name = "!"; // ! is unary, but the above operator check only checked binary ones
    }
  }

  // Check if an equal sign follows, for fused assignement operators such as `+=` or `[]=`
  if (try_consume(tokens, Symbol, SYM_EQ))
    name += "=";

  return name;
}

static auto parse_struct_decl(TokenIterator& tokens) -> unique_ptr<StructDecl> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_STRUCT);
  const string name = consume_word(tokens);
  if (!is_uword(name))
    throw std::runtime_error("Expected capitalized name for struct decl");

  auto type_args = vector<string>{};
  if (try_consume(tokens, Symbol, SYM_LT))
    consume_with_commas_until(tokens, Symbol, SYM_GT, [&] { type_args.push_back(consume_word(tokens)); });

  consume(tokens, Symbol, SYM_LPAREN);
  auto fields = vector<TypeName>{};
  consume_with_commas_until(tokens, Symbol, SYM_RPAREN, [&] { fields.push_back(std::move(*parse_type_name(tokens))); });

  auto body = vector<unique_ptr<Stmt>>{};
  auto body_begin = entry;

  require_separator(tokens);

  body_begin = tokens.begin();
  while (!try_consume(tokens, Word, KWD_END)) {
    body.push_back(parse_stmt(tokens));
    ignore_separator(tokens);
  }

  return make_ast<StructDecl>(entry, tokens, name, move(fields), type_args,
                              Compound(ts(body_begin, tokens), move(body)));
}

static auto parse_fn_decl(TokenIterator& tokens) -> unique_ptr<FnDecl> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_DEF);
  const string name = parse_fn_name(tokens);
  auto type_args = vector<string>{};
  if (try_consume(tokens, Symbol, SYM_LT))
    consume_with_commas_until(tokens, Symbol, SYM_GT, [&] { type_args.push_back(consume_word(tokens)); });

  consume(tokens, Symbol, SYM_LPAREN);

  auto args = vector<TypeName>{};
  consume_with_commas_until(tokens, Symbol, SYM_RPAREN, [&] { args.push_back(std::move(*parse_type_name(tokens))); });

  auto ret_type = try_parse_type(tokens);
  auto body = vector<unique_ptr<Stmt>>{};
  auto body_begin = entry;

  if (try_consume(tokens, Symbol, SYM_EQ)) { // A "short" function definition, consists of a single expression
    if (try_consume(tokens, Word, KWD_PRIMITIVE)) {
      consume(tokens, Symbol, SYM_LPAREN);
      auto primitive = consume_word(tokens);
      consume(tokens, Symbol, SYM_RPAREN);
      auto varargs = try_consume(tokens, Word, KWD_VARARGS);
      return make_ast<FnDecl>(entry, tokens, name, move(args), type_args, move(ret_type), varargs, primitive);
    }
    body_begin = tokens.begin();
    auto expr = parse_expr(tokens);
    body.push_back(make_ast<ReturnStmt>(entry, tokens, move(expr)));
  } else {
    require_separator(tokens);

    body_begin = tokens.begin();
    while (!try_consume(tokens, Word, KWD_END)) {
      body.push_back(parse_stmt(tokens));
      ignore_separator(tokens);
    }
  }

  return make_ast<FnDecl>(entry, tokens, name, move(args), type_args, move(ret_type),
                          Compound(ts(body_begin, tokens), move(body)));
}

static auto parse_var_decl(TokenIterator& tokens) -> unique_ptr<VarDecl> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_LET);
  const string name = consume_word(tokens);
  auto type = try_parse_type(tokens);

  consume(tokens, Symbol, SYM_EQ);

  auto init = parse_expr(tokens);

  return make_ast<VarDecl>(entry, tokens, name, move(type), move(init));
}

static auto parse_while_stmt(TokenIterator& tokens) -> unique_ptr<WhileStmt> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_WHILE);
  auto cond = parse_expr(tokens);

  ignore_separator(tokens);

  auto body_begin = tokens.begin();
  auto body = vector<unique_ptr<Stmt>>{};
  while (!try_consume(tokens, Word, KWD_END)) {
    body.push_back(parse_stmt(tokens));
    ignore_separator(tokens);
  }

  auto compound = Compound(ts(body_begin, tokens), move(body));

  return make_ast<WhileStmt>(entry, tokens, move(cond), std::move(compound));
}

static auto parse_return_stmt(TokenIterator& tokens) -> unique_ptr<ReturnStmt> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_RETURN);
  if (!tokens.at_end() && try_peek(tokens, 0, Separator))
    return make_ast<ReturnStmt>(entry, tokens, optional<unique_ptr<Expr>>{});

  auto expr = parse_expr(tokens);

  return make_ast<ReturnStmt>(entry, tokens, optional<unique_ptr<Expr>>{move(expr)});
}

static auto parse_if_stmt(TokenIterator& tokens) -> unique_ptr<IfStmt> {
  auto entry = tokens.begin();
  auto clause_begin = entry;
  consume(tokens, Word, KWD_IF);
  auto cond = parse_expr(tokens);
  if (!try_consume(tokens, Word, KWD_THEN))
    require_separator(tokens);

  auto current_entry = tokens.begin();
  auto else_entry = tokens.begin();
  auto clauses = vector<IfClause>{};
  auto current_body = vector<unique_ptr<Stmt>>{};
  auto else_body = vector<unique_ptr<Stmt>>{};
  bool in_else = false;

  while (true) {
    auto current_clause_begin = tokens.begin();
    if (try_consume(tokens, Word, KWD_END))
      break;
    if (try_consume(tokens, Word, KWD_ELSE)) {
      // An `else` followed by an `if` begins a new clause of the same if statement.
      if (!in_else && try_consume(tokens, Word, KWD_IF)) {
        clauses.emplace_back(ts(clause_begin, tokens), move(cond),
                             Compound(ts(current_entry, tokens), move(current_body)));
        current_body = vector<unique_ptr<Stmt>>{};
        cond = parse_expr(tokens);
        current_entry = tokens.begin();
        clause_begin = current_clause_begin;
      } else {
        in_else = true;
        else_entry = tokens.begin();
      }
      if (!try_consume(tokens, Word, KWD_THEN))
        require_separator(tokens);
    }
    auto st = parse_stmt(tokens);
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
    else_clause.emplace(ts(else_entry, tokens), move(else_body));

  return make_ast<IfStmt>(entry, tokens, move(clauses), move(else_clause));
}

static auto parse_number_expr(TokenIterator& tokens) -> unique_ptr<NumberExpr> {
  auto entry = tokens.begin();
  expect(tokens, Number);
  auto value = stoll(*next(tokens).m_payload->m_str);

  return make_ast<NumberExpr>(entry, 1, value);
}

static auto parse_string_expr(TokenIterator& tokens) -> unique_ptr<StringExpr> {
  auto entry = tokens.begin();
  expect(tokens, Token::Type::Literal);
  auto value = *next(tokens).m_payload->m_str;

  return make_ast<StringExpr>(entry, 1, value);
}

static auto parse_char_expr(TokenIterator& tokens) -> unique_ptr<CharExpr> {
  auto entry = tokens.begin();
  expect(tokens, Token::Type::Char);
  auto value = (*next(tokens).m_payload->m_str)[0];

  return make_ast<CharExpr>(entry, 1, value);
}

static auto parse_primary(TokenIterator& tokens) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  if (try_consume(tokens, Symbol, SYM_LPAREN)) {
    auto val = parse_expr(tokens);
    consume(tokens, Symbol, SYM_RPAREN);
    return val;
  }

  if (tokens->m_type == Number)
    return parse_number_expr(tokens);
  if (tokens->m_type == Token::Type::Literal)
    return parse_string_expr(tokens);
  if (tokens->m_type == Token::Type::Char)
    return parse_char_expr(tokens);
  if (try_consume(tokens, Word, KWD_TRUE))
    return make_ast<BoolExpr>(entry, tokens, true);
  if (try_consume(tokens, Word, KWD_FALSE))
    return make_ast<BoolExpr>(entry, tokens, false);

  if (tokens->m_type == Word) {
    if (try_peek_uword(tokens, 0)) {
      auto type = parse_type(tokens);
      if (try_consume(tokens, Symbol, SYM_LPAREN)) {
        auto call_args = vector<unique_ptr<Expr>>{};
        consume_with_commas_until(tokens, Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr(tokens)); });
        return make_ast<CtorExpr>(entry, tokens, move(type), move(call_args));
      }
      if (try_consume(tokens, Symbol, SYM_LBRACKET)) {
        auto slice_members = vector<unique_ptr<Expr>>{};
        consume_with_commas_until(tokens, Symbol, SYM_RBRACKET, [&] { slice_members.push_back(parse_expr(tokens)); });
        return make_ast<SliceExpr>(entry, tokens, move(type), move(slice_members));
      }
      throw std::runtime_error("Couldn't make an expression from here with a type");
    }
    auto name = consume_word(tokens);
    if (try_consume(tokens, Symbol, SYM_LPAREN)) {
      auto call_args = vector<unique_ptr<Expr>>{};
      consume_with_commas_until(tokens, Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr(tokens)); });
      return make_ast<CallExpr>(entry, tokens, name, move(call_args));
    }
    return make_ast<VarExpr>(entry, 1, name);
  }
  throw std::runtime_error("Couldn't make an expression from here");
}

static auto parse_receiver(TokenIterator& tokens, unique_ptr<Expr> receiver, auto receiver_entry) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  if (try_consume(tokens, Symbol, SYM_DOT)) {
    auto name = consume_word(tokens);
    auto call_args = vector<unique_ptr<Expr>>{};
    call_args.push_back(move(receiver));
    if (try_consume(tokens, Symbol, SYM_LPAREN)) { // A call with a dot `a.b(...)`
      consume_with_commas_until(tokens, Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr(tokens)); });
      auto call = make_ast<CallExpr>(entry + 1, tokens, name, move(call_args));
      return parse_receiver(tokens, move(call), receiver_entry);
    }
    if (try_consume(tokens, Symbol, SYM_EQ)) { // A setter `a.b = ...`
      auto value = parse_expr(tokens);
      call_args.push_back(move(value));
      auto call = make_ast<CallExpr>(entry + 1, tokens, name + '=', move(call_args));
      return parse_receiver(tokens, move(call), receiver_entry);
    }
    auto noarg_call = make_ast<CallExpr>(receiver_entry, tokens, name, move(call_args));
    return parse_receiver(tokens, move(noarg_call), receiver_entry);
  }
  if (try_consume(tokens, Symbol, SYM_EQ)) {
    auto value = parse_expr(tokens);
    auto assign = make_ast<AssignExpr>(receiver_entry, tokens, move(receiver), move(value));
    return parse_receiver(tokens, move(assign), receiver_entry);
  }
  if (try_consume(tokens, Symbol, SYM_LBRACKET)) {
    auto args = vector<unique_ptr<Expr>>{};
    args.push_back(move(receiver));
    args.push_back(parse_expr(tokens));
    consume(tokens, Symbol, SYM_RBRACKET);
    if (try_consume(tokens, Symbol, SYM_EQ)) {
      auto value = parse_expr(tokens);
      args.push_back(move(value));
      auto call = make_ast<CallExpr>(entry, tokens, "[]=", move(args));
      return parse_receiver(tokens, move(call), receiver_entry);
    }
    auto call = make_ast<CallExpr>(entry, tokens, "[]", move(args));
    return parse_receiver(tokens, move(call), receiver_entry);
  }
  if (try_consume(tokens, Symbol, SYM_COLON)) {
    consume(tokens, Symbol, SYM_COLON);
    auto field = consume_word(tokens);
    auto access = make_ast<FieldAccessExpr>(receiver_entry, tokens, move(receiver), field);
    return parse_receiver(tokens, move(access), receiver_entry);
  }
  return receiver;
}

static auto parse_receiver(TokenIterator& tokens) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  return parse_receiver(tokens, parse_primary(tokens), entry);
}

static auto parse_unary(TokenIterator& tokens) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  for (const auto& un_op : unary_operators()) {
    if (try_consume(tokens, Symbol, un_op)) {
      auto value = parse_receiver(tokens);
      auto args = vector<unique_ptr<Expr>>{};
      args.push_back(move(value));
      return make_ast<CallExpr>(entry, tokens, un_op, move(args));
    }
  }
  return parse_receiver(tokens);
}

static auto parse_operator(TokenIterator& tokens, size_t n = 0) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  const auto ops = operators();
  if (n == ops.size())
    return parse_unary(tokens);

  auto left = parse_operator(tokens, n + 1);
  while (true) {
    auto found_operator = false;
    for (const auto& op : ops[n]) {
      if (try_consume(tokens, Symbol, op)) {
        auto right = parse_operator(tokens, n + 1);
        auto args = vector<unique_ptr<Expr>>{};
        args.push_back(move(left));
        args.push_back(move(right));
        left = make_ast<CallExpr>(entry, tokens, op, move(args));
        found_operator = true;
        break;
      }
    }
    if (!found_operator)
      break;
  }
  return left;
}

static auto parse_stmt(TokenIterator& tokens) -> unique_ptr<Stmt> {
  auto stat = unique_ptr<Stmt>();

  if (tokens->is_keyword(KWD_DEF))
    stat = parse_fn_decl(tokens);
  else if (tokens->is_keyword(KWD_STRUCT))
    stat = parse_struct_decl(tokens);
  else if (tokens->is_keyword(KWD_LET))
    stat = parse_var_decl(tokens);
  else if (tokens->is_keyword(KWD_WHILE))
    stat = parse_while_stmt(tokens);
  else if (tokens->is_keyword(KWD_IF))
    stat = parse_if_stmt(tokens);
  else if (tokens->is_keyword(KWD_RETURN))
    stat = parse_return_stmt(tokens);
  else
    stat = parse_expr(tokens);

  require_separator(tokens);
  return stat;
}

static auto parse_type(TokenIterator& tokens, bool implicit_self) -> unique_ptr<Type> {
  auto entry = tokens.begin();
  auto base = [&]() -> unique_ptr<Type> {
    if (implicit_self || try_consume(tokens, Word, KWD_SELF_TYPE))
      return make_ast<SelfType>(entry, tokens);

    const string name = consume_word(tokens);
    if (!(is_uword(name)))
      throw std::runtime_error("Expected capitalized payload for simple type");

    return make_ast<SimpleType>(entry, tokens, name);
  }();
  while (true) {
    if (try_consume(tokens, Word, KWD_PTR)) {
      base = make_ast<QualType>(entry, tokens, move(base), Qualifier::Ptr);
    } else if (try_consume(tokens, Word, KWD_MUT)) {
      base = make_ast<QualType>(entry, tokens, move(base), Qualifier::Mut);
    } else if (try_peek(tokens, 0, Symbol, SYM_LBRACKET) && try_peek(tokens, 1, Symbol, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(tokens, Symbol, SYM_LBRACKET);
      consume(tokens, Symbol, SYM_RBRACKET);
      base = make_ast<QualType>(entry, tokens, move(base), Qualifier::Slice);
    } else {
      break;
    }
  }

  return base;
}

static auto try_parse_type(TokenIterator& tokens) -> optional<unique_ptr<Type>> {
  auto entry = tokens.begin();
  if (tokens->m_type != Word || !tokens->m_payload.has_value())
    return {};

  const string name = consume_word(tokens);
  if (make_atom(name) != KWD_SELF_TYPE && !is_uword(name))
    return {};

  auto base = [&]() -> unique_ptr<Type> {
    if (make_atom(name) == KWD_SELF_TYPE)
      return make_ast<SelfType>(entry, tokens);

    return make_ast<SimpleType>(entry, tokens, name);
  }();

  while (true) {
    if (try_consume(tokens, Word, KWD_PTR)) {
      base = make_ast<QualType>(entry, tokens, move(base), Qualifier::Ptr);
    } else if (try_consume(tokens, Word, KWD_MUT)) {
      base = make_ast<QualType>(entry, tokens, move(base), Qualifier::Mut);
    } else if (try_peek(tokens, 0, Symbol, SYM_LBRACKET) && try_peek(tokens, 1, Symbol, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(tokens, Symbol, SYM_LBRACKET);
      consume(tokens, Symbol, SYM_RBRACKET);
      base = make_ast<QualType>(entry, tokens, move(base), Qualifier::Slice);
    } else {
      break;
    }
  }

  return base;
}

static auto parse_type_name(TokenIterator& tokens) -> unique_ptr<TypeName> {
  auto entry = tokens.begin();
  if (try_consume(tokens, Word, KWD_SELF_ITEM)) {
    unique_ptr<Type> type = parse_type(tokens, /* implicit_self= */ true);
    return make_ast<TypeName>(entry, tokens, move(type), "self");
  }
  const string name = consume_word(tokens);
  unique_ptr<Type> type = parse_type(tokens);
  return make_ast<TypeName>(entry, tokens, move(type), name);
}

static auto parse_expr(TokenIterator& tokens) -> unique_ptr<Expr> { return parse_operator(tokens, 0); }
} // namespace yume::ast
