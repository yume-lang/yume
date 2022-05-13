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

static const Atom KEYWORD_DEF = "def"_a;
static const Atom KEYWORD_END = "end"_a;
static const Atom KEYWORD_LET = "let"_a;

static const Atom SYMBOL_EQ = "="_a;
static const Atom SYMBOL_LPAREN = "("_a;
static const Atom SYMBOL_RPAREN = ")"_a;

void ignore_separator(TokenIterator& tokens) {
  while (tokens->m_type == Separator) {
    tokens++;
  }
}

auto at(const source_location location = source_location::current()) -> string {
  return string(location.file_name()) + ":" + std::to_string(location.line()) + ":" +
         std::to_string(location.column()) + " in " + location.function_name();
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

  tokens++;
}

auto try_consume(TokenIterator& tokens, Token::Type tokenType, Atom payload) -> bool {
  if (tokens->m_type != tokenType || tokens->m_payload != payload) {
    return false;
  }

  tokens++;
  return true;
}

auto consume_word(TokenIterator& tokens,
                  const source_location location = source_location::current()) -> string {
  ignore_separator(tokens);
  if (tokens->m_type != Word) {
    throw std::runtime_error("Expected word"s + " at " + at(location));
  }
  return *tokens++->m_payload;
}

auto Program::parse(TokenIterator& tokens) -> unique_ptr<Program> {
  auto statements = vector<unique_ptr<Statement>>{};
  statements.push_back(move(Statement::parse(tokens)));

  return std::make_unique<Program>(statements);
}
void Program::visit(Visitor& visitor) const { visitor.visit(m_body); }

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

  auto compound = std::make_unique<Compound>(body);
  return std::make_unique<FnDeclStatement>(name, args, move(return_type), compound);
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

auto Statement::parse(TokenIterator& tokens) -> unique_ptr<Statement> {
  auto stat = unique_ptr<Statement>();

  if (tokens->is_keyword(KEYWORD_DEF)) {
    stat = FnDeclStatement::parse(++tokens);
  } else if (tokens->is_keyword(KEYWORD_LET)) {
    stat = VarDeclStatement::parse(++tokens);
  } else {
    throw std::runtime_error("Can't make a statement from here!");
  }
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

auto Expr::parse(TokenIterator& tokens) -> unique_ptr<Expr> {
  if (tokens->m_type == Number) {
    return NumberExpr::parse(tokens);
  }
  return nullptr;
}

auto NumberExpr::parse(TokenIterator& tokens) -> unique_ptr<NumberExpr> {
  expect(tokens, Number);
  auto value = stoll(*tokens++->m_payload->m_str);

  return std::make_unique<NumberExpr>(value);
}
void NumberExpr::visit(Visitor& visitor) const { visitor.visit(describe()); }
} // namespace yume::ast
