#include "parser.hpp"

#include "ast/ast.hpp"
#include "diagnostic/notes.hpp"
#include "qualifier.hpp"
#include <algorithm>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>

namespace yume::ast::parser {
auto Parser::ignore_separator([[maybe_unused]] const source_location location) -> bool {
  bool found_separator = false;
  while (!tokens.at_end() && tokens->type == Separator) {
#ifdef YUME_SPEW_CONSUMED_TOKENS
    errs() << "consumed " << *tokens << " at " << at(location) << "\n";
#endif
    ++tokens;
    found_separator = true;
  }
  return found_separator;
}

void Parser::expect(Token::Type token_type, const source_location location) const {
  if (tokens.at_end())
    emit_fatal_and_terminate() << "Expected token type " << Token::type_name(token_type) << ", got the end of the file";

  if (tokens->type != token_type) {
    emit_fatal_and_terminate() << "Expected token type " << Token::type_name(token_type) << ", got "
                               << to_string(*tokens) << " at " + at(location);
  }
}

void Parser::require_separator(const source_location location) {
  if (!tokens.at_end() && tokens->type != Token::Type::EndOfFile)
    expect(Separator, location);
  ignore_separator(location);
}

auto Parser::to_string(Token token) -> string {
  string str{};
  llvm::raw_string_ostream(str) << token;
  return str;
}

void Parser::consume(TokenAtom token_atom, const source_location location) {
  auto [token_type, payload] = token_atom;
  ignore_separator();
  if (tokens.at_end()) {
    emit_fatal_and_terminate() << "Expected token type " << Token::type_name(token_type)
                               << " for payload " + string(payload) << ", got the end of the file at " << at(location);
  }

  if (tokens->type != token_type) {
    emit_fatal_and_terminate() << "Expected token type " << Token::type_name(token_type) << " for payload "
                               << string(payload) << ", got " << to_string(*tokens) << " at " << at(location);
  }
  if (tokens->payload != payload) {
    emit_fatal_and_terminate() << "Expected payload atom " << string(payload) << ", got " << to_string(*tokens)
                               << " at " << at(location);
  }

#ifdef YUME_SPEW_CONSUMED_TOKENS
  errs() << "consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
}

auto Parser::try_consume(TokenAtom token_atom, [[maybe_unused]] const source_location location) -> bool {
  auto [token_type, payload] = token_atom;
  if (tokens.at_end() || tokens->type != token_type || tokens->payload != payload)
    return false;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  errs() << "try_consume: " << *tokens << " at " << at(location) << "\n";
#endif

  tokens++;
  return true;
}

auto Parser::try_peek(int ahead, TokenAtom token_atom, [[maybe_unused]] const source_location location) const -> bool {
  auto [token_type, payload] = token_atom;
  if (tokens.at_end())
    return false;

  auto token = tokens + ahead;
  if (token.at_end())
    return false;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << " " << string(payload)
         << ", got " << *token << " at " << at(location) << "\n";
#endif

  return (token->type == token_type) && (token->payload == payload);
}

auto Parser::try_peek(int ahead, Token::Type token_type, [[maybe_unused]] const source_location location) const
    -> bool {
  if (tokens.at_end())
    return false;

  auto token = tokens + ahead;
  if (token.at_end())
    return false;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << ", got " << *token
         << " at " << at(location) << "\n";
#endif

  return token->type == token_type;
}

auto Parser::next([[maybe_unused]] const source_location location) -> Token {
  auto tok = *tokens++;
#ifdef YUME_SPEW_CONSUMED_TOKENS
  errs() << "next: " << tok << " at " << at(location) << "\n";
#endif
  return tok;
}

auto Parser::assert_payload_next([[maybe_unused]] const source_location location) -> Atom {
  auto payload = tokens->payload;
  if (!payload) {
    emit_fatal_and_terminate() << "Expected a payload, but wasn't found: " << to_string(*tokens) << " at "
                               << at(location);
    llvm_unreachable("Fatal error should have terminated by now");
  }

  next(location);
  return payload.value();
}

auto Parser::consume_word(const source_location location) -> string {
  ignore_separator();
  if (tokens.at_end())
    emit_fatal_and_terminate() << "Expected word, got the end of the file";
  if (tokens->type != Word)
    emit_fatal_and_terminate() << "Expected word, got " << to_string(*tokens) << " at " << at(location);

  return string(assert_payload_next());
}

auto Parser::try_peek_uword(int ahead, [[maybe_unused]] const source_location location) const -> bool {
  auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
  errs() << "try_peek ahead by " << ahead << ": expected uword, got " << *token << " at " << at(location) << "\n";
#endif

  auto payload = token->payload;
  return token->type == Word && payload.has_value() && is_uword(payload.value());
}

auto Parser::parse_stmt(bool require_sep) -> unique_ptr<Stmt> {
  auto stat = unique_ptr<Stmt>();

  if (tokens->is_a(KWD_DEF))
    stat = parse_fn_or_ctor_decl();
  else if (tokens->is_a(KWD_STRUCT) || tokens->is_a(KWD_INTERFACE))
    stat = parse_struct_decl();
  else if (tokens->is_a(KWD_LET))
    stat = parse_var_decl();
  else if (tokens->is_a(KWD_CONST))
    stat = parse_const_decl();
  else if (tokens->is_a(KWD_WHILE))
    stat = parse_while_stmt();
  else if (tokens->is_a(KWD_IF))
    stat = parse_if_stmt();
  else if (tokens->is_a(KWD_RETURN))
    stat = parse_return_stmt();
  else
    stat = parse_expr();

  if (require_sep)
    require_separator();
  return stat;
}

auto Parser::try_parse_function_type() -> optional<unique_ptr<FunctionType>> {
  auto entry = tokens.begin();
  if (try_consume(SYM_LPAREN)) {
    auto args = vector<AnyType>{};
    consume_with_commas_until(SYM_ARROW, [&] { args.emplace_back(parse_type()); });
    auto fn_ptr = try_consume(KWD_PTR);
    auto ret = OptionalType{try_parse_type()};
    consume(SYM_RPAREN);
    return ast_ptr<FunctionType>(entry, move(ret), move(args), fn_ptr);
  }
  return {};
}

auto Parser::parse_type(bool implicit_self) -> unique_ptr<Type> {
  if (!implicit_self) {
    if (auto maybe_fn_type = try_parse_function_type(); maybe_fn_type.has_value())
      return move(*maybe_fn_type);
  }

  auto entry = tokens.begin();
  auto base = [&]() -> unique_ptr<Type> {
    if (implicit_self || try_consume(KWD_SELF_TYPE))
      return ast_ptr<SelfType>(entry);

    const string name = consume_word();
    if (!is_uword(name))
      emit_fatal_and_terminate() << "Expected capitalized payload for simple type";

    return ast_ptr<SimpleType>(entry, name);
  }();
  while (true) {
    if (try_consume(KWD_PTR)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ptr);
    } else if (try_consume(KWD_MUT)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Mut);
    } else if (try_consume(KWD_REF)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ref);
    } else if (try_consume(KWD_TYPE)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Type);
    } else if (try_peek(0, SYM_LBRACKET) && try_peek(1, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(SYM_LBRACKET);
      consume(SYM_RBRACKET);

      auto slice_ty = ast_ptr<SimpleType>(entry, "Slice");
      auto generic_args = vector<AnyTypeOrExpr>{};
      generic_args.emplace_back(move(base));
      base = ast_ptr<TemplatedType>(entry, move(slice_ty), move(generic_args));
    } else if (try_consume(SYM_LBRACE)) {
      auto generic_args = vector<AnyTypeOrExpr>{};
      consume_with_commas_until(SYM_RBRACE, [&] {
        if (auto type = try_parse_type(); type.has_value())
          generic_args.emplace_back(move(*type));
        else
          generic_args.emplace_back(parse_expr());
      });

      base = ast_ptr<TemplatedType>(entry, move(base), move(generic_args));
    } else {
      break;
    }
  }

  return base;
}

auto Parser::try_parse_type() -> optional<unique_ptr<Type>> {
  if (auto maybe_fn_type = try_parse_function_type(); maybe_fn_type.has_value())
    return maybe_fn_type;

  auto entry = tokens.begin();
  if (tokens->type != Word || !tokens->payload.has_value())
    return {};

  if (!try_peek_uword(0))
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
    } else if (try_consume(KWD_REF)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ref);
    } else if (try_consume(KWD_TYPE)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Type);
    } else if (try_peek(0, SYM_LBRACKET) && try_peek(1, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(SYM_LBRACKET);
      consume(SYM_RBRACKET);

      auto slice_ty = ast_ptr<SimpleType>(entry, "Slice");
      auto generic_args = vector<AnyTypeOrExpr>{};
      generic_args.emplace_back(move(base));
      base = ast_ptr<TemplatedType>(entry, move(slice_ty), move(generic_args));
    } else if (try_consume(SYM_LBRACE)) {
      auto generic_args = vector<AnyTypeOrExpr>{};
      consume_with_commas_until(SYM_RBRACE, [&] {
        if (auto type = try_parse_type(); type.has_value())
          generic_args.emplace_back(move(*type));
        else
          generic_args.emplace_back(parse_expr());
      });

      base = ast_ptr<TemplatedType>(entry, move(base), move(generic_args));
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

auto Parser::parse_logical_or() -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  auto left = parse_logical_and();
  if (try_consume(SYM_OR_OR)) {
    auto right = parse_logical_or();
    left = ast_ptr<BinaryLogicExpr>(entry, SYM_OR_OR.second, move(left), move(right));
  }
  return left;
}

auto Parser::parse_logical_and() -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  auto left = parse_operator();
  if (try_consume(SYM_AND_AND)) {
    auto right = parse_logical_and();
    left = ast_ptr<BinaryLogicExpr>(entry, SYM_AND_AND.second, move(left), move(right));
  }
  return left;
}

auto Parser::parse_expr() -> unique_ptr<Expr> { return parse_logical_or(); }

auto Parser::parse_fn_name() -> string {
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

  // Check if an equal sign follows, for fused assignment operators such as `+=` or `[]=`
  if (try_consume(SYM_EQ))
    name += "=";

  return name;
}

auto Parser::parse_struct_decl() -> unique_ptr<StructDecl> {
  auto entry = tokens.begin();

  bool interface = try_consume(KWD_INTERFACE);
  if (!interface)
    consume(KWD_STRUCT);

  const string name = consume_word();
  if (!is_uword(name))
    emit_fatal_and_terminate() << "Expected capitalized name for struct decl";

  auto type_args = parse_generic_type_params();

  auto fields = vector<TypeName>{};
  if (try_consume(SYM_LPAREN))
    consume_with_commas_until(SYM_RPAREN, [&] { fields.push_back(move(*parse_type_name())); });

  auto implements = OptionalType{};
  if (try_consume(KWD_IS))
    implements = parse_type();

  require_separator();

  auto body = vector<AnyStmt>{};
  auto body_begin = tokens.begin();
  while (!try_consume(KWD_END)) {
    body.emplace_back(parse_stmt());
    ignore_separator();
  }

  return ast_ptr<StructDecl>(entry, name, move(fields), move(type_args), make_ast<Compound>(body_begin, move(body)),
                             move(implements), interface);
}

auto Parser::parse_fn_arg() -> FnArg {
  auto entry = tokens.begin();
  if (try_consume(SYM_COLON_COLON)) {
    auto field_name = consume_word();
    AnyType proxy_type = ast_ptr<ProxyType>(entry, field_name);
    auto proxied_arg = ast_ptr<TypeName>(entry, move(proxy_type), field_name);

    auto implicit_field = ast_ptr<FieldAccessExpr>(entry, std::nullopt, field_name);
    auto arg_var = ast_ptr<VarExpr>(entry, field_name);
    auto extra_assign = ast_ptr<AssignExpr>(entry, move(implicit_field), move(arg_var));

    return {move(proxied_arg), move(extra_assign)};
  }

  return {parse_type_name(), std::nullopt};
}

auto Parser::parse_generic_type_params() -> vector<GenericParam> {
  auto type_args = vector<GenericParam>{};
  if (try_consume(SYM_LBRACE)) {
    consume_with_commas_until(SYM_RBRACE, [&] {
      auto entry = tokens.begin();
      auto name = consume_word();
      if (is_uword(name)) {
        if (!try_consume(KWD_TYPE))
          emit_note(-1, diagnostic::Severity::Warn) << "Type parameter with no specifier will be deprecated";
        type_args.push_back(make_ast<GenericParam>(entry, std::nullopt, name));
      } else {
        auto type = parse_type();
        type_args.push_back(make_ast<GenericParam>(entry, move(type), name));
      }
    });
  }

  return type_args;
}

auto Parser::parse_fn_or_ctor_decl() -> unique_ptr<Stmt> {
  if (try_peek(1, SYM_COLON))
    return parse_ctor_decl();
  return parse_fn_decl();
}

auto Parser::parse_fn_decl() -> unique_ptr<FnDecl> {
  auto entry = tokens.begin();

  consume(KWD_DEF);

  auto annotations = std::set<string>{};
  while (try_consume(SYM_AT))
    annotations.emplace(consume_word());

  const string name = parse_fn_name();
  auto type_args = parse_generic_type_params();

  consume(SYM_LPAREN);

  auto args = vector<TypeName>{};
  auto body = vector<AnyStmt>{};

  consume_with_commas_until(SYM_RPAREN, [&] {
    auto arg = parse_fn_arg();
    args.emplace_back(move(*arg.type_name));
    if (arg.extra_body)
      body.emplace_back(move(arg.extra_body));
  });

  auto ret_type = OptionalType{try_parse_type()};
  auto body_begin = entry;

  if (try_consume(SYM_EQ)) { // A "short" function definition, consists of a single expression
    if (try_consume(KWD_PRIMITIVE)) {
      consume(SYM_LPAREN);
      auto primitive = consume_word();
      consume(SYM_RPAREN);
      return ast_ptr<FnDecl>(entry, name, move(args), move(type_args), move(ret_type), primitive, move(annotations));
    }
    if (try_consume(KWD_EXTERN)) {
      // consume(SYM_LPAREN);
      // auto primitive = consume_word();
      // consume(SYM_RPAREN);
      auto varargs = try_consume(KWD_VARARGS);
      return ast_ptr<FnDecl>(entry, name, move(args), move(type_args), move(ret_type),
                             FnDecl::extern_decl_t{name, varargs}, move(annotations));
    }
    if (try_consume(KWD_ABSTRACT)) {
      if (args.empty()) {
        // A no-arg abstract method wouldn't be able to be called, because the self type is required to perform
        // dispatch. Add it here silently. It cannot actually be used, so it will be discarded when passed to the impl
        // method.
        // HACK: This currently basically only works by chance, as there is no logic in place to verify type
        // compatibility between interface methods and their implementations. As such, this invented self pointer just
        // magically vanishes, where as if it would be properly type checked, they wouldn't match.
        // HACK: This implicit variable is a hack itself, as it makes it indistinguishable from an abstract method
        // with an explicit self parameter.
        // HACK: As per the hack mentioned above, the arg is given an empty name "", which is checked for later, under
        // the assumption that regular code can't write an empty name. Relying on a specific, special name is still
        // dirty, though.
        args.emplace_back(make_ast<TypeName>(entry, ast_ptr<SelfType>(entry), ""));
      }
      return ast_ptr<FnDecl>(entry, name, move(args), move(type_args), move(ret_type), FnDecl::abstract_decl_t{},
                             move(annotations));
    }
    body_begin = tokens.begin();
    auto expr = parse_expr();
    body.emplace_back(ast_ptr<ReturnStmt>(entry, move(expr)));
  } else {
    if (!try_peek(0, KWD_END)) // Allow `end` to be on the same line
      require_separator();

    body_begin = tokens.begin();
    while (!try_consume(KWD_END)) {
      body.emplace_back(parse_stmt());
      ignore_separator();
    }
  }

  return ast_ptr<FnDecl>(entry, name, move(args), move(type_args), move(ret_type),
                         make_ast<Compound>(body_begin, move(body)), move(annotations));
}

auto Parser::parse_ctor_decl() -> unique_ptr<CtorDecl> {
  auto entry = tokens.begin();

  consume(KWD_DEF);
  consume(SYM_COLON);
  consume(KWD_NEW);
  consume(SYM_LPAREN);

  auto args = vector<TypeName>{};
  auto body = vector<AnyStmt>{};

  consume_with_commas_until(SYM_RPAREN, [&] {
    auto arg = parse_fn_arg();
    args.emplace_back(move(*arg.type_name));
    if (arg.extra_body)
      body.emplace_back(move(arg.extra_body));
  });

  if (!try_peek(0, KWD_END)) // Allow `end` to be on the same line
    require_separator();

  auto body_begin = tokens.begin();
  while (!try_consume(KWD_END)) {
    body.emplace_back(parse_stmt());
    ignore_separator();
  }

  return ast_ptr<CtorDecl>(entry, move(args), make_ast<Compound>(body_begin, move(body)));
}

auto Parser::parse_var_decl() -> unique_ptr<VarDecl> {
  auto entry = tokens.begin();

  consume(KWD_LET);
  const string name = consume_word();
  auto type = OptionalType{try_parse_type()};

  consume(SYM_EQ);

  auto init = parse_expr();

  return ast_ptr<VarDecl>(entry, name, move(type), move(init));
}

auto Parser::parse_const_decl() -> unique_ptr<ConstDecl> {
  auto entry = tokens.begin();

  consume(KWD_CONST);
  const string name = consume_word();
  auto type = AnyType{parse_type()};

  consume(SYM_EQ);

  auto init = parse_expr();

  return ast_ptr<ConstDecl>(entry, name, move(type), move(init));
}

auto Parser::parse_while_stmt() -> unique_ptr<WhileStmt> {
  auto entry = tokens.begin();

  consume(KWD_WHILE);
  auto cond = parse_expr();

  ignore_separator();

  auto body_begin = tokens.begin();
  auto body = vector<AnyStmt>{};
  while (!try_consume(KWD_END)) {
    body.emplace_back(parse_stmt());
    ignore_separator();
  }

  auto compound = make_ast<Compound>(body_begin, move(body));

  return ast_ptr<WhileStmt>(entry, move(cond), move(compound));
}

auto Parser::parse_return_stmt() -> unique_ptr<ReturnStmt> {
  auto entry = tokens.begin();

  consume(KWD_RETURN);
  if (!tokens.at_end() && try_peek(0, Separator))
    return ast_ptr<ReturnStmt>(entry, std::nullopt);

  auto expr = parse_expr();

  return ast_ptr<ReturnStmt>(entry, move(expr));
}

auto Parser::parse_if_stmt() -> unique_ptr<IfStmt> {
  auto entry = tokens.begin();
  auto clause_begin = entry;
  consume(KWD_IF);
  auto cond = parse_expr();
  if (!try_consume(KWD_THEN))
    require_separator();

  auto current_entry = tokens.begin();
  auto else_entry = tokens.begin();
  auto clauses = vector<IfClause>{};
  auto current_body = vector<AnyStmt>{};
  auto else_body = vector<AnyStmt>{};
  bool in_else = false;

  while (true) {
    auto current_clause_begin = tokens.begin();
    if (try_consume(KWD_END))
      break;
    if (try_consume(KWD_ELSE)) {
      // An `else` followed by an `if` begins a new clause of the same if statement.
      if (!in_else && try_consume(KWD_IF)) {
        clauses.emplace_back(ts(clause_begin), move(cond), make_ast<Compound>(current_entry, move(current_body)));
        current_body = vector<AnyStmt>{};
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
    if (in_else)
      else_body.emplace_back(move(st));
    else
      current_body.emplace_back(move(st));
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

auto Parser::parse_number_expr() -> unique_ptr<NumberExpr> {
  static constexpr int BASE_16 = 16;
  static constexpr int BASE_10 = 10;
  auto entry = tokens.begin();
  expect(Number);

  auto literal = string(assert_payload_next());
  int64_t value = literal.starts_with("0x"sv) ? stoll(literal, nullptr, BASE_16) : stoll(literal, nullptr, BASE_10);

  return ast_ptr<NumberExpr>({entry, 1}, value);
}

auto Parser::parse_string_expr() -> unique_ptr<StringExpr> {
  auto entry = tokens.begin();
  expect(Token::Type::Literal);

  auto value = string(assert_payload_next());

  return ast_ptr<StringExpr>({entry, 1}, value);
}

auto Parser::parse_char_expr() -> unique_ptr<CharExpr> {
  auto entry = tokens.begin();
  expect(Token::Type::Char);

  auto value = string(assert_payload_next())[0];

  return ast_ptr<CharExpr>({entry, 1}, value);
}

auto Parser::parse_primary() -> unique_ptr<Expr> {
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
  if (try_consume(SYM_COLON_COLON))
    return ast_ptr<FieldAccessExpr>(entry, std::nullopt, consume_word());
  if (try_consume(SYM_DOLLAR))
    return ast_ptr<ConstExpr>(entry, consume_word(), std::nullopt);

  if (tokens->type == Word) {
    if (try_peek_uword(0)) {
      if (try_peek(1, SYM_COLON_COLON)) {
        auto parent = consume_word();
        consume(SYM_COLON_COLON);
        return ast_ptr<ConstExpr>(entry, consume_word(), parent);
      }

      auto type = parse_type();
      if (try_consume(SYM_LPAREN)) {
        auto call_args = collect_with_commas_until<AnyExpr>(SYM_RPAREN, &Parser::parse_expr);
        return ast_ptr<CtorExpr>(entry, move(type), move(call_args));
      }
      if (try_consume(SYM_COLON)) {
        if (try_consume(SYM_LBRACKET)) {
          auto slice_members = collect_with_commas_until<AnyExpr>(SYM_RBRACKET, &Parser::parse_expr);
          return ast_ptr<SliceExpr>(entry, move(type), move(slice_members));
        }
      }
      if (try_consume(SYM_DOT)) {
        if (try_peek_uword(0))
          emit_fatal_and_terminate() << "Nested types aren't yet implemented";

        auto name = consume_word();
        auto call_args = vector<AnyExpr>{};
        if (try_consume(SYM_LPAREN))
          collect_with_commas_until(SYM_RPAREN, &Parser::parse_expr, call_args);
        return ast_ptr<CallExpr>(entry, name, move(type), move(call_args));
      }

      return ast_ptr<TypeExpr>(entry, move(type));
    }
    auto name = consume_word();
    if (try_consume(SYM_LPAREN)) {
      auto call_args = collect_with_commas_until<AnyExpr>(SYM_RPAREN, &Parser::parse_expr);
      return ast_ptr<CallExpr>(entry, name, std::nullopt, move(call_args));
    }
    return ast_ptr<VarExpr>({entry, 1}, name);
  }
  emit_fatal_and_terminate() << "Couldn't make an expression from here";
  llvm_unreachable("Fatal error encountered");
}

auto Parser::parse_receiver(unique_ptr<Expr> receiver, VectorTokenIterator receiver_entry) -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  if (try_consume(SYM_DOT)) {
    auto name = consume_word();
    auto call_args = vector<AnyExpr>{};
    call_args.emplace_back(move(receiver));
    if (try_consume(SYM_LPAREN)) { // A call with a dot `a.b(...)`
      collect_with_commas_until(SYM_RPAREN, &Parser::parse_expr, call_args);
      auto call = ast_ptr<CallExpr>(entry + 1, name, std::nullopt, move(call_args));
      return parse_receiver(move(call), receiver_entry);
    }
    if (try_consume(SYM_EQ)) { // A setter `a.b = ...`
      auto value = parse_expr();
      call_args.emplace_back(move(value));
      auto call = ast_ptr<CallExpr>(entry + 1, name + '=', std::nullopt, move(call_args));
      return parse_receiver(move(call), receiver_entry);
    }
    auto noarg_call = ast_ptr<CallExpr>(receiver_entry, name, std::nullopt, move(call_args));
    return parse_receiver(move(noarg_call), receiver_entry);
  }
  if (try_consume(SYM_EQ)) {
    auto value = parse_expr();
    auto assign = ast_ptr<AssignExpr>(receiver_entry, move(receiver), move(value));
    return parse_receiver(move(assign), receiver_entry);
  }
  if (try_consume(SYM_LBRACKET)) {
    auto args = vector<AnyExpr>{};
    args.emplace_back(move(receiver));
    args.emplace_back(parse_expr());
    consume(SYM_RBRACKET);
    if (try_consume(SYM_EQ)) {
      auto value = parse_expr();
      args.emplace_back(move(value));
      auto call = ast_ptr<CallExpr>(entry, "[]=", std::nullopt, move(args));
      return parse_receiver(move(call), receiver_entry);
    }
    auto call = ast_ptr<CallExpr>(entry, "[]", std::nullopt, move(args));
    return parse_receiver(move(call), receiver_entry);
  }
  if (try_consume(SYM_COLON_COLON)) {
    auto field = consume_word();
    auto access = ast_ptr<FieldAccessExpr>(receiver_entry, move(receiver), field);
    return parse_receiver(move(access), receiver_entry);
  }
  if (try_consume(SYM_ARROW)) {
    consume(SYM_LPAREN);
    auto call_args = vector<AnyExpr>{};
    call_args.emplace_back(move(receiver));
    collect_with_commas_until(SYM_RPAREN, &Parser::parse_expr, call_args);
    auto call = ast_ptr<CallExpr>(entry, "->", std::nullopt, move(call_args));
    return parse_receiver(move(call), receiver_entry);
  }
  return receiver;
}

auto Parser::parse_lambda() -> unique_ptr<LambdaExpr> {
  auto entry = tokens.begin();

  consume(KWD_DEF);

  auto annotations = std::set<string>{};
  while (try_consume(SYM_AT))
    annotations.emplace(consume_word());

  consume(SYM_LPAREN);

  auto args = vector<TypeName>{};
  consume_with_commas_until(SYM_RPAREN, [&] {
    auto arg = parse_type_name();
    args.emplace_back(move(*arg));
  });

  auto ret_type = OptionalType{try_parse_type()};
  auto body_begin = entry;
  auto body = vector<AnyStmt>{};

  if (try_consume(SYM_EQ)) { // A "short" function definition, consists of a single expression
    body_begin = tokens.begin();
    auto expr = parse_expr();
    body.emplace_back(ast_ptr<ReturnStmt>(entry, move(expr)));
  } else {
    if (!try_peek(0, KWD_END)) // Allow `end` to be on the same line
      require_separator();

    body_begin = tokens.begin();
    while (!try_consume(KWD_END)) {
      body.emplace_back(parse_stmt());
      ignore_separator();
    }
  }

  return ast_ptr<LambdaExpr>(entry, move(args), move(ret_type), make_ast<Compound>(body_begin, move(body)),
                             move(annotations));
}

auto Parser::parse_receiver() -> unique_ptr<Expr> {
  if (tokens->is_a(KWD_DEF))
    return parse_lambda();

  auto entry = tokens.begin();
  return parse_receiver(parse_primary(), entry);
}

auto Parser::parse_unary() -> unique_ptr<Expr> {
  auto entry = tokens.begin();
  for (const auto& un_op : unary_operators()) {
    if (try_consume(un_op)) {
      auto value = parse_receiver();
      auto args = vector<AnyExpr>{};
      args.emplace_back(move(value));
      return ast_ptr<CallExpr>(entry, string(std::get<Atom>(un_op)), std::nullopt, move(args));
    }
  }
  return parse_receiver();
}
} // namespace yume::ast::parser

namespace yume::ast {
auto Program::parse(TokenIterator& tokens, diagnostic::NotesHolder& notes) -> unique_ptr<Program> {
  auto parser = parser::Parser{tokens, notes};
  parser.ignore_separator();
  auto entry = tokens.begin();

  auto statements = vector<AnyStmt>{};
  while (tokens->type != Token::Type::EndOfFile)
    statements.emplace_back(parser.parse_stmt());
  tokens++; // Consume the EOF token

  return make_unique<Program>(parser.ts(entry), move(statements));
}
} // namespace yume::ast
