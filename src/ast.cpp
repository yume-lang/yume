//
// Created by rymiel on 5/8/22.
//

#include "ast.hpp"

#if __has_include(<source_location>) && __has_builtin(__builtin_source_location)
#include <source_location>
#define yume_has_source_location 1
#elif __has_include(<experimental/source_location>)
#include <experimental/source_location>
#define yume_has_source_location -1
#else
#define yume_has_source_location 0
#endif

#include <memory>
#include <sstream>

namespace yume::ast {

#if yume_has_source_location == 1
using std::source_location;
#elif yume_has_source_location == -1
using std::experimental::source_location;
#else
struct source_location {
  constexpr auto file_name() const { return "??"; }     // NOLINT
  constexpr auto function_name() const { return "??"; } // NOLINT
  constexpr auto line() const { return -1; }            // NOLINT
  constexpr auto column() const { return -1; }          // NOLINT
  static inline constexpr auto current() { return source_location{}; }
};
#endif

constexpr static auto Symbol = Token::Type::Symbol;
constexpr static auto Word = Token::Type::Word;
constexpr static auto Separator = Token::Type::Separator;
constexpr static auto Number = Token::Type::Number;

static const Atom KWD_IF = "if"_a;
static const Atom KWD_DEF = "def"_a;
static const Atom KWD_END = "end"_a;
static const Atom KWD_LET = "let"_a;
static const Atom KWD_PTR = "ptr"_a;
static const Atom KWD_ELSE = "else"_a;
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

auto operators() {
  const static vector<vector<Atom>> OPERATORS = {
      {SYM_EQ_EQ, SYM_NEQ, SYM_GT, SYM_LT},
      {SYM_PLUS, SYM_MINUS},
      {SYM_PERCENT, SYM_SLASH_SLASH, SYM_STAR},
  };
  return OPERATORS;
}

auto at(const source_location location = source_location::current()) -> string {
  return string(location.file_name()) + ":" + std::to_string(location.line()) + ":" +
         std::to_string(location.column()) + " in " + location.function_name();
}

auto to_string(Token token) -> string {
  std::stringstream ss{};
  ss << token;
  return ss.str();
}

void ignore_separator(TokenIterator& tokens,
                      [[maybe_unused]] const source_location location = source_location::current()) {
  while (!tokens.at_end() && tokens->m_type == Separator) {
#ifdef YUME_SPEW_CONSUMED_TOKENS
    std::cerr << "consumed " << *tokens << " at " << at(location) << "\n";
#endif
    ++tokens;
  }
}

void expect(TokenIterator& tokens, Token::Type token_type,
            const source_location location = source_location::current()) {
  if (tokens->m_type != token_type) {
    throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got " + to_string(*tokens) +
                             " at " + at(location));
  }
}

void require_separator(TokenIterator& tokens, const source_location location = source_location::current()) {
  expect(tokens, Separator, location);
  ignore_separator(tokens, location);
}

void consume(TokenIterator& tokens, Token::Type token_type, Atom payload,
             const source_location location = source_location::current()) {
  ignore_separator(tokens);
  expect(tokens, token_type, location);
  if (tokens->m_payload != payload) {
    throw std::runtime_error("Expected payload atom "s + string(payload) + ", got " + to_string(*tokens) + " at " +
                             at(location));
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
}

auto try_consume(TokenIterator& tokens, Token::Type tokenType, Atom payload,
                 [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  if (tokens->m_type != tokenType || tokens->m_payload != payload) {
    return false;
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "try_consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
  return true;
}

auto try_peek(TokenIterator& tokens, int ahead, Token::Type token_type, Atom payload,
              [[maybe_unused]] const source_location location = source_location::current()) -> bool {
  auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << " " << string(payload)
            << ", got " << *token << " at " << at(location) << "\n";
#endif

  return !(token->m_type != token_type || token->m_payload != payload);
}

void consume_with_separators_until(TokenIterator& tokens, Token::Type token_type, Atom payload, auto action,
                                   const source_location location = source_location::current()) {
  int i = 0;
  while (!try_consume(tokens, token_type, payload, location)) {
    if (i++ > 0) {
      consume(tokens, Symbol, SYM_COMMA, location);
    }
    action();
  }
}

auto next(TokenIterator& tokens, [[maybe_unused]] const source_location location = source_location::current())
    -> Token {
  auto tok = *tokens++;
#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "next: " << tok << " at " << at(location) << "\n";
#endif
  return tok;
}

auto consume_word(TokenIterator& tokens, const source_location location = source_location::current()) -> string {
  ignore_separator(tokens);
  if (tokens->m_type != Word) {
    throw std::runtime_error("Expected word, got "s + to_string(*tokens) + " at " + at(location));
  }
  return *next(tokens, location).m_payload;
}

static auto parse_stmt(TokenIterator& tokens) -> unique_ptr<Stmt>;
static auto parse_expr(TokenIterator& tokens) -> unique_ptr<Expr>;
static auto parse_type_name(TokenIterator& tokens) -> unique_ptr<TypeName>;
static auto try_parse_type(TokenIterator& tokens) -> optional<unique_ptr<Type>>;

auto Program::parse(TokenIterator& tokens) -> unique_ptr<Program> {
  auto entry = tokens.begin();

  auto statements = vector<unique_ptr<Stmt>>{};
  while (!tokens.at_end()) {
    statements.push_back(parse_stmt(tokens));
  }

  return std::make_unique<Program>(span{entry, tokens.begin()}, statements);
}

static auto parse_fn_name(TokenIterator& tokens) -> string {
  string name{};
  if (tokens->m_type == Word) {
    name = consume_word(tokens);
  } else if (tokens->m_type == Symbol) {
    bool found_op = false;
    for (const auto& op_row : operators()) {
      for (const auto& op : op_row) {
        if (try_consume(tokens, Symbol, op)) {
          found_op = true;
          name = op;
          break;
        }
      }
      if (found_op) {
        break;
      }
    }

    if (try_consume(tokens, Symbol, SYM_LBRACKET)) {
      consume(tokens, Symbol, SYM_RBRACKET);
      name = "[]";
    } else if (try_consume(tokens, Symbol, SYM_BANG)) {
      name = "!";
    }
  }

  if (try_consume(tokens, Symbol, SYM_EQ)) {
    name += "=";
  }

  return name;
}

static auto parse_struct_decl(TokenIterator& tokens) -> unique_ptr<StructDecl> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_STRUCT);
  const string name = consume_word(tokens);
  if (isupper(name.front()) == 0) {
    throw std::runtime_error("Expected capitalized name for struct decl");
  }
  auto type_args = vector<string>{};
  if (try_consume(tokens, Symbol, SYM_LT)) {
    consume_with_separators_until(tokens, Symbol, SYM_GT, [&] { type_args.push_back(consume_word(tokens)); });
  }

  consume(tokens, Symbol, SYM_LPAREN);
  auto fields = vector<TypeName>{};
  consume_with_separators_until(tokens, Symbol, SYM_RPAREN,
                                [&] { fields.push_back(std::move(*parse_type_name(tokens))); });

  auto body = vector<unique_ptr<Stmt>>{};
  auto body_begin = entry;

  require_separator(tokens);

  body_begin = tokens.begin();
  while (!try_consume(tokens, Word, KWD_END)) {
    body.push_back(parse_stmt(tokens));
    ignore_separator(tokens);
  }

  return std::make_unique<StructDecl>(span{entry, tokens.begin()}, name, fields, type_args,
                                      Compound(span{body_begin, tokens.begin()}, body));
}

static auto parse_fn_decl(TokenIterator& tokens) -> unique_ptr<FnDecl> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_DEF);
  const string name = parse_fn_name(tokens);
  auto type_args = vector<string>{};
  if (try_consume(tokens, Symbol, SYM_LT)) {
    consume_with_separators_until(tokens, Symbol, SYM_GT, [&] { type_args.push_back(consume_word(tokens)); });
  }
  consume(tokens, Symbol, SYM_LPAREN);

  auto args = vector<unique_ptr<TypeName>>{};
  consume_with_separators_until(tokens, Symbol, SYM_RPAREN, [&] { args.push_back(parse_type_name(tokens)); });

  auto return_type = try_parse_type(tokens);
  auto body = vector<unique_ptr<Stmt>>{};
  auto body_begin = entry;

  if (try_consume(tokens, Symbol, SYM_EQ)) {
    if (try_consume(tokens, Word, KWD_PRIMITIVE)) {
      consume(tokens, Symbol, SYM_LPAREN);
      auto primitive = consume_word(tokens);
      consume(tokens, Symbol, SYM_RPAREN);
      auto varargs = try_consume(tokens, Word, KWD_VARARGS);
      return std::make_unique<FnDecl>(span{entry, tokens.begin()}, name, args, type_args, move(return_type), varargs,
                                      primitive);
    }
    body_begin = tokens.begin();
    auto expr = parse_expr(tokens);
    body.push_back(std::make_unique<ReturnStmt>(span{entry, tokens.begin()}, move(expr)));
  } else {
    require_separator(tokens);

    body_begin = tokens.begin();
    while (!try_consume(tokens, Word, KWD_END)) {
      body.push_back(parse_stmt(tokens));
      ignore_separator(tokens);
    }
  }

  return std::make_unique<FnDecl>(span{entry, tokens.begin()}, name, args, type_args, move(return_type),
                                  std::make_unique<Compound>(span{body_begin, tokens.begin()}, body));
}

static auto parse_var_decl(TokenIterator& tokens) -> unique_ptr<VarDecl> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_LET);
  const string name = consume_word(tokens);
  auto type = try_parse_type(tokens);

  consume(tokens, Symbol, SYM_EQ);

  auto init = parse_expr(tokens);

  return std::make_unique<VarDecl>(span{entry, tokens.begin()}, name, move(type), move(init));
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

  auto compound = std::make_unique<Compound>(span{body_begin, tokens.begin()}, body);

  return std::make_unique<WhileStmt>(span{entry, tokens.begin()}, move(cond), move(compound));
}

static auto parse_return_stmt(TokenIterator& tokens) -> unique_ptr<ReturnStmt> {
  auto entry = tokens.begin();

  consume(tokens, Word, KWD_RETURN);
  auto expr = parse_expr(tokens);

  return std::make_unique<ReturnStmt>(span{entry, tokens.begin()}, optional<unique_ptr<Expr>>{move(expr)});
}

static auto parse_if_stmt(TokenIterator& tokens) -> unique_ptr<IfStmt> {
  auto entry = tokens.begin();
  auto clause_begin = entry;
  consume(tokens, Word, KWD_IF);
  auto cond = parse_expr(tokens);
  require_separator(tokens); // todo(rymiel): compact `then`

  auto current_entry = tokens.begin();
  auto else_entry = tokens.begin();
  auto clauses = vector<unique_ptr<IfClause>>{};
  auto current_body = vector<unique_ptr<Stmt>>{};
  auto else_body = vector<unique_ptr<Stmt>>{};
  bool in_else = false;

  while (true) {
    auto current_clause_begin = tokens.begin();
    if (try_consume(tokens, Word, KWD_END)) {
      break;
    }
    if (try_consume(tokens, Word, KWD_ELSE)) {
      if (!in_else && try_consume(tokens, Word, KWD_IF)) {
        clauses.push_back(
            std::make_unique<IfClause>(span{clause_begin, tokens.begin()}, move(cond),
                                       std::make_unique<Compound>(span{current_entry, tokens.begin()}, current_body)));
        current_body = vector<unique_ptr<Stmt>>{};
        cond = parse_expr(tokens);
        current_entry = tokens.begin();
        clause_begin = current_clause_begin;
      } else {
        in_else = true;
        else_entry = tokens.begin();
      }
      require_separator(tokens);
    }
    auto st = parse_stmt(tokens);
    if (in_else) {
      else_body.push_back(move(st));
    } else {
      current_body.push_back(move(st));
    }
  }

  if (else_body.empty()) {
    else_entry = tokens.begin();
  }

  clauses.push_back(
      std::make_unique<IfClause>(span{clause_begin, else_entry - 1}, move(cond),
                                 std::make_unique<Compound>(span{current_entry, else_entry - 1}, current_body)));

  auto else_clause = optional<unique_ptr<Compound>>{};
  if (!else_body.empty()) {
    else_clause = std::make_unique<Compound>(span{else_entry, tokens.begin()}, else_body);
  }

  // ignore_separator(tokens);

  return std::make_unique<IfStmt>(span{entry, tokens.begin()}, clauses, move(else_clause));
}

static auto parse_number_expr(TokenIterator& tokens) -> unique_ptr<NumberExpr> {
  auto entry = tokens.begin();
  expect(tokens, Number);
  auto value = stoll(*next(tokens).m_payload->m_str);

  return std::make_unique<NumberExpr>(span{entry, 1}, value);
}

static auto parse_string_expr(TokenIterator& tokens) -> unique_ptr<StringExpr> {
  auto entry = tokens.begin();
  expect(tokens, Token::Type::Literal);
  auto value = *next(tokens).m_payload->m_str;

  return std::make_unique<StringExpr>(span{entry, 1}, value);
}

static auto parse_char_expr(TokenIterator& tokens) -> unique_ptr<CharExpr> {
  auto entry = tokens.begin();
  expect(tokens, Token::Type::Char);
  auto value = (*next(tokens).m_payload->m_str)[0];

  return std::make_unique<CharExpr>(span{entry, 1}, value);
}

static auto parse_primary(TokenIterator& tokens) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  if (tokens->m_type == Number) {
    return parse_number_expr(tokens);
  }
  if (tokens->m_type == Token::Type::Literal) {
    return parse_string_expr(tokens);
  }
  if (tokens->m_type == Token::Type::Char) {
    return parse_char_expr(tokens);
  }
  if (tokens->m_type == Word) {
    auto name = consume_word(tokens);
    if (try_consume(tokens, Symbol, SYM_LPAREN)) {
      auto call_args = vector<unique_ptr<Expr>>{};
      consume_with_separators_until(tokens, Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr(tokens)); });
      return std::make_unique<CallExpr>(span{entry, tokens.begin()}, name, call_args);
    }
    return std::make_unique<VarExpr>(span{entry, 1}, name);
  }
  tokens++;
  return nullptr;
}

static auto parse_receiver(TokenIterator& tokens, unique_ptr<Expr> receiver, auto receiver_entry) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  if (try_consume(tokens, Symbol, SYM_DOT)) {
    auto name = consume_word(tokens);
    if (try_consume(tokens, Symbol, SYM_LPAREN)) {
      auto call_args = vector<unique_ptr<Expr>>{};
      consume_with_separators_until(tokens, Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr(tokens)); });
      auto call = std::make_unique<CallExpr>(span{entry + 1, tokens.begin()}, name, call_args);
      return parse_receiver(tokens, move(call), receiver_entry);
    }
    auto access = std::make_unique<FieldAccessExpr>(span{receiver_entry, tokens.begin()}, receiver, name);
    return parse_receiver(tokens, move(access), receiver_entry);
  }
  if (try_consume(tokens, Symbol, SYM_EQ)) {
    auto value = parse_expr(tokens);
    auto assign = std::make_unique<AssignExpr>(span{receiver_entry, tokens.begin()}, receiver, value);
    return parse_receiver(tokens, move(assign), receiver_entry);
  }
  if (try_consume(tokens, Symbol, SYM_LBRACKET)) {
    auto args = vector<unique_ptr<Expr>>{};
    args.push_back(move(receiver));
    args.push_back(parse_expr(tokens));
    consume(tokens, Symbol, SYM_RBRACKET);
    auto call = std::make_unique<CallExpr>(span{entry, tokens.begin()}, "[]", args);
    return parse_receiver(tokens, move(call), receiver_entry);
  }
  return receiver;
}

static auto parse_receiver(TokenIterator& tokens) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  return parse_receiver(tokens, parse_primary(tokens), entry);
}

static auto parse_unary(TokenIterator& tokens) -> unique_ptr<Expr> {
  // TODO
  return parse_receiver(tokens);
}

static auto parse_operator(TokenIterator& tokens, size_t n = 0) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  const auto ops = operators();
  if (n == ops.size()) {
    return parse_unary(tokens);
  }
  auto left = parse_operator(tokens, n + 1);
  while (true) {
    auto found_operator = false;
    for (const auto& op : ops[n]) {
      if (try_consume(tokens, Symbol, op)) {
        auto right = parse_operator(tokens, n + 1);
        auto args = vector<unique_ptr<Expr>>{};
        args.push_back(move(left));
        args.push_back(move(right));
        left = std::make_unique<CallExpr>(span{entry, tokens.begin()}, op, args);
        found_operator = true;
        break;
      }
    }
    if (!found_operator) {
      break;
    }
  }
  return left;
}

static auto parse_stmt(TokenIterator& tokens) -> unique_ptr<Stmt> {
  auto stat = unique_ptr<Stmt>();

  if (tokens->is_keyword(KWD_DEF)) {
    stat = parse_fn_decl(tokens);
  } else if (tokens->is_keyword(KWD_STRUCT)) {
    stat = parse_struct_decl(tokens);
  } else if (tokens->is_keyword(KWD_LET)) {
    stat = parse_var_decl(tokens);
  } else if (tokens->is_keyword(KWD_WHILE)) {
    stat = parse_while_stmt(tokens);
  } else if (tokens->is_keyword(KWD_IF)) {
    stat = parse_if_stmt(tokens);
  } else if (tokens->is_keyword(KWD_RETURN)) {
    stat = parse_return_stmt(tokens);
  } else {
    stat = parse_expr(tokens);
  }

  require_separator(tokens);
  return stat;
}

static auto parse_type(TokenIterator& tokens) -> unique_ptr<Type> {
  auto entry = tokens.begin();
  const string name = consume_word(tokens);
  if (isupper(name.front()) == 0) {
    throw std::runtime_error("Expected capitalized payload for simple type");
  }

  unique_ptr<Type> base = std::make_unique<SimpleType>(span{entry, tokens.begin()}, name);
  while (true) {
    if (try_consume(tokens, Word, KWD_PTR)) {
      base = std::make_unique<QualType>(span{entry, tokens.begin()}, move(base), QualType::Qualifier::Ptr);
    } else if (try_consume(tokens, Symbol, SYM_LBRACKET)) {
      consume(tokens, Symbol, SYM_RBRACKET);
      base = std::make_unique<QualType>(span{entry, tokens.begin()}, move(base), QualType::Qualifier::Slice);
    } else {
      break;
    }
  }

  return base;
}

static auto try_parse_type(TokenIterator& tokens) -> optional<unique_ptr<Type>> {
  auto entry = tokens.begin();
  if (tokens->m_type != Word || !tokens->m_payload.has_value()) {
    return {};
  }
  const string name = consume_word(tokens);
  if (isupper(name.front()) == 0) {
    return {};
  }

  unique_ptr<Type> base = std::make_unique<SimpleType>(span{entry, tokens.begin()}, name);
  while (true) {
    if (try_consume(tokens, Word, KWD_PTR)) {
      base = std::make_unique<QualType>(span{entry, tokens.begin()}, move(base), QualType::Qualifier::Ptr);
    } else if (try_peek(tokens, 0, Symbol, SYM_LBRACKET) && try_peek(tokens, 1, Symbol, SYM_RBRACKET)) {
      consume(tokens, Symbol, SYM_LBRACKET);
      consume(tokens, Symbol, SYM_RBRACKET);
      base = std::make_unique<QualType>(span{entry, tokens.begin()}, move(base), QualType::Qualifier::Slice);
    } else {
      break;
    }
  }

  return base;
}

static auto parse_type_name(TokenIterator& tokens) -> unique_ptr<TypeName> {
  auto entry = tokens.begin();
  const string name = consume_word(tokens);
  unique_ptr<Type> type = parse_type(tokens);
  return std::make_unique<TypeName>(span{entry, tokens.begin()}, type, name);
}

static auto parse_expr(TokenIterator& tokens) -> unique_ptr<Expr> { return parse_operator(tokens, 0); }

void IfStmt::visit(Visitor& visitor) const { visitor.visit(m_clauses).visit(m_else_clause, "else"); }
void IfClause::visit(Visitor& visitor) const { visitor.visit(m_cond).visit(m_body); }
void NumberExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }
void StringExpr::visit(Visitor& visitor) const { visitor.visit(m_val); }
void CharExpr::visit(Visitor& visitor) const { visitor.visit(string{static_cast<char>(m_val)}); }
void ReturnStmt::visit(Visitor& visitor) const { visitor.visit(m_expr); }
void WhileStmt::visit(Visitor& visitor) const { visitor.visit(m_cond).visit(m_body); }
void VarDecl::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_type).visit(m_init); }
void FnDecl::visit(Visitor& visitor) const {
  visitor.visit(m_name).visit(m_args, "arg").visit(m_type_args, "type arg").visit(m_ret, "ret");
  if (const auto* s = get_if<string>(&m_body); s) {
    visitor.visit(*s, "primitive");
  } else {
    visitor.visit(get<unique_ptr<Compound>>(m_body));
  }
  if (m_varargs) {
    visitor.visit("varargs");
  }
}
void StructDecl::visit(Visitor& visitor) const {
  visitor.visit(m_name).visit(m_fields, "field").visit(m_type_args, "type arg").visit(m_body);
}
void SimpleType::visit(Visitor& visitor) const { visitor.visit(m_name); }
void QualType::visit(Visitor& visitor) const { visitor.visit(m_base, describe().c_str()); }
void TypeName::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_type); }
void Compound::visit(Visitor& visitor) const { visitor.visit(m_body); }
void VarExpr::visit(Visitor& visitor) const { visitor.visit(m_name); }
void CallExpr::visit(Visitor& visitor) const { visitor.visit(m_name).visit(m_args); }
void AssignExpr::visit(Visitor& visitor) const { visitor.visit(m_target).visit(m_value); }
void FieldAccessExpr::visit(Visitor& visitor) const { visitor.visit(m_base).visit(m_field); }
void Program::visit(Visitor& visitor) const { visitor.visit(m_body); }
} // namespace yume::ast
