//
// Created by rymiel on 5/8/22.
//

#include "ast.hpp"

#include <experimental/source_location>
#include <memory>

namespace yume::ast {
using namespace yume::atom_literal;
using namespace std::literals::string_literals;
using std::experimental::source_location;

constexpr static auto Symbol = Token::Type::Symbol;
constexpr static auto Word = Token::Type::Word;
constexpr static auto Separator = Token::Type::Separator;
constexpr static auto Number = Token::Type::Number;

static const Atom KEYWORD_IF = "if"_a;
static const Atom KEYWORD_DEF = "def"_a;
static const Atom KEYWORD_END = "end"_a;
static const Atom KEYWORD_LET = "let"_a;
static const Atom KEYWORD_ELSE = "else"_a;
static const Atom KEYWORD_WHILE = "while"_a;
static const Atom KEYWORD_RETURN = "return"_a;

static const Atom SYMBOL_COMMA = ","_a;
static const Atom SYMBOL_EQ = "="_a;
static const Atom SYMBOL_LPAREN = "("_a;
static const Atom SYMBOL_RPAREN = ")"_a;
static const Atom SYMBOL_EQ_EQ = "=="_a;
static const Atom SYMBOL_NEQ = "!="_a;
static const Atom SYMBOL_GT = ">"_a;
static const Atom SYMBOL_LT = "<"_a;
static const Atom SYMBOL_PLUS = "+"_a;
static const Atom SYMBOL_MINUS = "-"_a;
static const Atom SYMBOL_PERCENT = "%"_a;
static const Atom SYMBOL_SLASH_SLASH = "//"_a;
static const Atom SYMBOL_ASTERISK = "*"_a;

auto at(const source_location location = source_location::current()) -> string {
  return string(location.file_name()) + ":" + std::to_string(location.line()) + ":" +
         std::to_string(location.column()) + " in " + location.function_name();
}

void ignore_separator(TokenIterator& tokens, const source_location location = source_location::current()) {
  while (tokens->m_type == Separator) {
#ifdef YUME_SPEW_CONSUMED_TOKENS
    std::cerr << "consumed " << *tokens << " at " << at(location) << "\n";
#endif
    ++tokens;
  }
}

void expect(TokenIterator& tokens, Token::Type token_type,
            const source_location location = source_location::current()) {
  if (tokens->m_type != token_type) {
    throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got " +
                             Token::type_name(tokens->m_type) + " at " + at(location));
  }
}

void consume(TokenIterator& tokens, Token::Type token_type, Atom payload,
             const source_location location = source_location::current()) {
  ignore_separator(tokens);
  expect(tokens, token_type);
  if (tokens->m_payload != payload) {
    throw std::runtime_error("Expected payload atom "s + string(payload) + ", got " + string(*tokens->m_payload) +
                             " at " + at(location));
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
}

auto try_consume(TokenIterator& tokens, Token::Type tokenType, Atom payload,
                 const source_location location = source_location::current()) -> bool {
  if (tokens->m_type != tokenType || tokens->m_payload != payload) {
    return false;
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "try_consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
  return true;
}

auto next(TokenIterator& tokens, const source_location location = source_location::current()) -> Token {
  auto tok = *tokens++;
#ifdef YUME_SPEW_CONSUMED_TOKENS
  std::cerr << "next: " << tok << " at " << at(location) << "\n";
#endif
  return tok;
}

auto consume_word(TokenIterator& tokens, const source_location location = source_location::current()) -> string {
  ignore_separator(tokens);
  if (tokens->m_type != Word) {
    throw std::runtime_error("Expected word"s + " at " + at(location));
  }
  return *next(tokens, location).m_payload;
}

auto Program::parse(TokenIterator& tokens) -> unique_ptr<Program> {
  auto statements = vector<unique_ptr<Statement>>{};
  while (!tokens.end()) {
    statements.push_back(move(Statement::parse(tokens)));
  }

  return std::make_unique<Program>(statements);
}
void Program::visit(Visitor& visitor) const { visitor.visit(m_body); }

auto ExprStatement::parse(TokenIterator& tokens) -> unique_ptr<ExprStatement> {
  auto expr = Expr::parse(tokens);

  if (!expr) { // TODO(rymiel): remove check when all statement types are implemented
    return nullptr;
  }

  return std::make_unique<ExprStatement>(move(expr));
}
void ExprStatement::visit(Visitor& visitor) const { visitor.visit(m_expr); }

auto FnDeclStatement::parse(TokenIterator& tokens) -> unique_ptr<FnDeclStatement> {
  const string name = consume_word(tokens);
  consume(tokens, Symbol, SYMBOL_LPAREN);

  auto args = vector<unique_ptr<TypeName>>{};
  while (!try_consume(tokens, Symbol, SYMBOL_RPAREN)) {
    args.push_back(TypeName::parse(tokens));
  }

  auto return_type = SimpleType::parse(tokens);
  ignore_separator(tokens);

  auto body = vector<unique_ptr<Statement>>{};
  while (!try_consume(tokens, Word, KEYWORD_END)) {
    auto stat = Statement::parse(tokens);
    if (stat != nullptr) { // TODO(rymiel): remove check when all statement types are implemented
      body.push_back(move(stat));
    }
    ignore_separator(tokens);
  }

  return std::make_unique<FnDeclStatement>(name, args, move(return_type), std::make_unique<Compound>(body));
}
void FnDeclStatement::visit(Visitor& visitor) const { visitor.visit(m_name, m_args, m_ret, m_body); }

auto VarDeclStatement::parse(TokenIterator& tokens) -> unique_ptr<VarDeclStatement> {
  const string name = consume_word(tokens);
  auto type = SimpleType::try_parse(tokens);

  consume(tokens, Symbol, SYMBOL_EQ);

  auto init = Expr::parse(tokens);

  return std::make_unique<VarDeclStatement>(name, move(type), move(init));
}
void VarDeclStatement::visit(Visitor& visitor) const { visitor.visit(m_name, m_type, m_init); }

auto WhileStatement::parse(TokenIterator& tokens) -> unique_ptr<WhileStatement> {
  auto cond = Expr::parse(tokens);

  ignore_separator(tokens);

  auto body = vector<unique_ptr<Statement>>{};
  while (!try_consume(tokens, Word, KEYWORD_END)) {
    auto stat = Statement::parse(tokens);
    if (stat != nullptr) { // TODO(rymiel): remove check when all statement types are implemented
      body.push_back(move(stat));
    }
    ignore_separator(tokens);
  }

  auto compound = std::make_unique<Compound>(body);

  return std::make_unique<WhileStatement>(move(cond), move(compound));
}
void WhileStatement::visit(Visitor& visitor) const { visitor.visit(m_cond, m_body); }

auto ReturnStatement::parse(TokenIterator& tokens) -> unique_ptr<ReturnStatement> {
  auto expr = Expr::parse(tokens);
  ignore_separator(tokens);

  return std::make_unique<ReturnStatement>(optional<unique_ptr<Expr>>{move(expr)});
}
void ReturnStatement::visit(Visitor& visitor) const { visitor.visit(m_expr); }

auto IfStatement::parse(TokenIterator& tokens) -> unique_ptr<IfStatement> {
  auto cond = Expr::parse(tokens);
  ignore_separator(tokens);

  auto clauses = vector<unique_ptr<IfClause>>{};
  auto current_body = vector<unique_ptr<Statement>>{};
  auto else_body = vector<unique_ptr<Statement>>{};
  bool in_else = false;

  while (true) {
    if (try_consume(tokens, Word, KEYWORD_END)) {
      break;
    }
    if (try_consume(tokens, Word, KEYWORD_ELSE)) {
      if (in_else && try_consume(tokens, Word, KEYWORD_IF)) {
        clauses.push_back(std::make_unique<IfClause>(move(cond), std::make_unique<Compound>(current_body)));
        current_body = vector<unique_ptr<Statement>>{};
        cond = Expr::parse(tokens);
      } else {
        in_else = true;
      }
      ignore_separator(tokens);
    }
    auto st = Statement::parse(tokens);
    if (in_else) {
      else_body.push_back(move(st));
    } else {
      current_body.push_back(move(st));
    }
  }

  clauses.push_back(std::make_unique<IfClause>(move(cond), std::make_unique<Compound>(current_body)));

  auto else_clause = optional<unique_ptr<Compound>>{};
  if (!else_body.empty()) {
    else_clause = std::make_unique<Compound>(else_body);
  }

  ignore_separator(tokens);

  return std::make_unique<IfStatement>(clauses, move(else_clause));
}
void IfStatement::visit(Visitor& visitor) const { visitor.visit(m_clauses, m_else_clause); }
void IfClause::visit(Visitor& visitor) const { visitor.visit(m_cond, m_body); }

auto operators() {
  // TODO: why does clang-format do this?
  const static vector<vector<Atom>> OPERATORS = {
      {SYMBOL_EQ_EQ,   SYMBOL_NEQ,         SYMBOL_GT, SYMBOL_LT},
      {SYMBOL_PLUS,              SYMBOL_MINUS                     },
      {SYMBOL_PERCENT, SYMBOL_SLASH_SLASH, SYMBOL_ASTERISK     },
  };
  return OPERATORS;
}

auto parse_primary(TokenIterator& tokens) -> unique_ptr<Expr> {
  if (tokens->m_type == Number) {
    return NumberExpr::parse(tokens);
  }
  if (tokens->m_type == Token::Type::Literal) {
    return StringExpr::parse(tokens);
  }
  if (tokens->m_type == Word) {
    auto name = consume_word(tokens);
    if (try_consume(tokens, Symbol, SYMBOL_LPAREN)) {
      int n = 0;
      auto args = vector<unique_ptr<Expr>>{};
      while (!try_consume(tokens, Symbol, SYMBOL_RPAREN)) {
        if (n++ > 0) {
          consume(tokens, Symbol, SYMBOL_COMMA);
        }
        args.push_back(Expr::parse(tokens));
      }
      return std::make_unique<CallExpr>(name, args);
    }
    return std::make_unique<VarExpr>(name);
  }
  tokens++;
  return nullptr;
}

auto parse_receiver(TokenIterator& tokens, unique_ptr<Expr> receiver) -> unique_ptr<Expr> {
  // TODO
  if (try_consume(tokens, Symbol, SYMBOL_EQ)) {
    auto value = Expr::parse(tokens);
    auto assign = std::make_unique<AssignExpr>(receiver, value);
    return parse_receiver(tokens, move(assign));
  }
  return receiver;
}

auto parse_receiver(TokenIterator& tokens) -> unique_ptr<Expr> { return parse_receiver(tokens, parse_primary(tokens)); }

auto parse_unary(TokenIterator& tokens) -> unique_ptr<Expr> {
  // TODO
  return parse_receiver(tokens);
}

auto parse_operator(TokenIterator& tokens, int n = 0) -> unique_ptr<Expr> {
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
        left = std::make_unique<CallExpr>(op, args);
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

auto Statement::parse(TokenIterator& tokens) -> unique_ptr<Statement> {
  auto stat = unique_ptr<Statement>();

  if (tokens->is_keyword(KEYWORD_DEF)) {
    stat = FnDeclStatement::parse(++tokens);
  } else if (tokens->is_keyword(KEYWORD_LET)) {
    stat = VarDeclStatement::parse(++tokens);
  } else if (tokens->is_keyword(KEYWORD_WHILE)) {
    stat = WhileStatement::parse(++tokens);
  } else if (tokens->is_keyword(KEYWORD_IF)) {
    stat = IfStatement::parse(++tokens);
  } else if (tokens->is_keyword(KEYWORD_RETURN)) {
    stat = ReturnStatement::parse(++tokens);
  } else {
    stat = ExprStatement::parse(tokens);
  }

  ignore_separator(tokens);
  return stat;
}

auto SimpleType::parse(TokenIterator& tokens) -> unique_ptr<SimpleType> {
  const string name = consume_word(tokens);
  if (isupper(name.front()) == 0) {
    throw std::runtime_error("Expected capitalized payload for simple type");
  }

  return std::make_unique<SimpleType>(name);
}

auto SimpleType::try_parse(TokenIterator& tokens) -> optional<unique_ptr<SimpleType>> {
  if (tokens->m_type != Word || !tokens->m_payload.has_value()) {
    return {};
  }
  const string name = consume_word(tokens);
  if (isupper(name.front()) == 0) {
    return {};
  }

  return std::make_unique<SimpleType>(name);
}
void SimpleType::visit(Visitor& visitor) const { visitor.visit(m_name); }

auto TypeName::parse(TokenIterator& tokens) -> unique_ptr<TypeName> {
  const string name = consume_word(tokens);
  unique_ptr<Type> type = SimpleType::parse(tokens);
  return std::make_unique<TypeName>(type, name);
}
void TypeName::visit(Visitor& visitor) const { visitor.visit(m_name, m_type); }
void Compound::visit(Visitor& visitor) const { visitor.visit(m_body); }

auto Expr::parse(TokenIterator& tokens) -> unique_ptr<Expr> { return parse_operator(tokens, 0); }

auto NumberExpr::parse(TokenIterator& tokens) -> unique_ptr<NumberExpr> {
  expect(tokens, Number);
  auto value = stoll(*next(tokens).m_payload->m_str);

  return std::make_unique<NumberExpr>(value);
}
void NumberExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }

auto StringExpr::parse(TokenIterator& tokens) -> unique_ptr<StringExpr> {
  expect(tokens, Token::Type::Literal);
  auto value = *next(tokens).m_payload->m_str;

  return std::make_unique<StringExpr>(value);
}
void StringExpr::visit(Visitor& visitor) const { visitor.visit(m_val); }

void VarExpr::visit(Visitor& visitor) const { visitor.visit(m_name); }

void CallExpr::visit(Visitor& visitor) const { visitor.visit(m_name, m_args); }

void AssignExpr::visit(Visitor& visitor) const { visitor.visit(m_target, m_value); }
} // namespace yume::ast
