#include "ast.hpp"

#include "diagnostic/source_location.hpp"
#include "qualifier.hpp"
#include "token.hpp"
#include "type.hpp"
#include "util.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <llvm/Support/raw_ostream.h>
#include <memory>

namespace yume::ast {

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
struct TokenRange {
  span<Token> m_span;

  constexpr TokenRange(auto&& begin, int end) : m_span{begin.base(), static_cast<size_t>(end)} {}
  constexpr TokenRange(auto&& begin, auto&& end) : m_span{begin.base(), end.base()} {}

  operator span<Token>() const { return m_span; }
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

  static auto operators() {
    const static std::array OPERATORS = {
        vector<Atom>{SYM_EQ_EQ, SYM_NEQ, SYM_GT, SYM_LT},
        vector<Atom>{SYM_PLUS, SYM_MINUS},
        vector<Atom>{SYM_PERCENT, SYM_SLASH_SLASH, SYM_STAR},
    };
    return OPERATORS;
  }

  static auto unary_operators() {
    const static vector<Atom> UNARY_OPERATORS = {
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
  void expect(Token::Type token_type, const source_location location = source_location::current()) const {
    if (tokens.at_end())
      throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got the end of the file");

    if (tokens->m_type != token_type)
      throw std::runtime_error("Expected token type "s + Token::type_name(token_type) + ", got " + to_string(*tokens) +
                               " at " + at(location));
  }

  /// Consume all subsequent `Separator` tokens. Throws if none were found.
  void require_separator(const source_location location = source_location::current()) {
    if (!tokens.at_end())
      expect(Separator, location);
    ignore_separator(location);
  }

  /// Consume a token of type `token_type` with the given `payload`. Throws if it wasn't encountered.
  void consume(Token::Type token_type, Atom payload, const source_location location = source_location::current()) {
    ignore_separator();
    expect(token_type, location);
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
  auto try_consume(Token::Type tokenType, Atom payload,
                   [[maybe_unused]] const source_location location = source_location::current()) -> bool {
    if (tokens.at_end() || tokens->m_type != tokenType || tokens->m_payload != payload) {
      return false;
    }

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_consume: " << *tokens << " at " << at(location) << "\n";
#endif

    tokens++;
    return true;
  }

  /// Check if the token ahead by `ahead` is of type `token_type` with the given `payload`.
  [[nodiscard]] auto try_peek(int ahead, Token::Type token_type, Atom payload,
                              [[maybe_unused]] const source_location location = source_location::current()) const
      -> bool {
    if (tokens.at_end())
      return false;

    auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_peek ahead by " << ahead << ": expected " << Token::type_name(token_type) << " "
                 << string(payload) << ", got " << *token << " at " << at(location) << "\n";
#endif

    return !(token->m_type != token_type || token->m_payload != payload);
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

    return token->m_type == token_type;
  }

  /// Consume tokens until a token of type `token_type` with the given `payload` is encountered.
  /// `action` (a no-arg function) is called every time. Between each call, a comma is expected.
  void consume_with_commas_until(Token::Type token_type, Atom payload, auto action,
                                 const source_location location = source_location::current()) {
    int i = 0;
    while (!try_consume(token_type, payload, location)) {
      if (i++ > 0)
        consume(Symbol, SYM_COMMA, location);
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
    if (tokens->m_type != Word)
      throw std::runtime_error("Expected word, got the end of the file");
    if (tokens->m_type != Word)
      throw std::runtime_error("Expected word, got "s + to_string(*tokens) + " at " + at(location));

    return *next(location).m_payload;
  }

  /// Check if the string begins with a capital letter. Used for types, as all types must be capitalized.
  static auto is_uword(const string& word) -> bool { return isupper(word.front()) != 0; }

  /// Check if the ahead by `ahead` is a capitalized word.
  [[nodiscard]] auto try_peek_uword(int ahead,
                                    [[maybe_unused]] const source_location location = source_location::current()) const
      -> bool {
    auto token = tokens + ahead;

#ifdef YUME_SPEW_CONSUMED_TOKENS
    llvm::errs() << "try_peek ahead by " << ahead << ": expected uword, got " << *token << " at " << at(location)
                 << "\n";
#endif

    return token->m_type == Word && is_uword(token->m_payload.value());
  }

  auto parse_stmt() -> unique_ptr<Stmt>;
  auto parse_expr() -> unique_ptr<Expr>;
  auto parse_type(bool implicit_self = false) -> unique_ptr<Type>;
  auto parse_type_name() -> unique_ptr<TypeName>;
  auto try_parse_type() -> optional<unique_ptr<Type>>;

  auto parse_fn_name() -> string {
    string name{};
    if (tokens->m_type == Word) {
      name = consume_word();
    } else if (tokens->m_type == Symbol) {
      // Try to parse an operator name, as in `def +()`
      bool found_op = false;
      for (const auto& op_row : operators()) {
        for (const auto& op : op_row) {
          if (try_consume(Symbol, op)) {
            found_op = true;
            name = op;
            break;
          }
        }
        if (found_op)
          break;
      }

      // If an operator wasn't found, try parse the operator []
      if (try_consume(Symbol, SYM_LBRACKET)) {
        consume(Symbol, SYM_RBRACKET);
        name = "[]";
      } else if (try_consume(Symbol, SYM_BANG)) {
        name = "!"; // ! is unary, but the above operator check only checked binary ones
      }
    }

    // Check if an equal sign follows, for fused assignement operators such as `+=` or `[]=`
    if (try_consume(Symbol, SYM_EQ))
      name += "=";

    return name;
  }

  auto parse_struct_decl() -> unique_ptr<StructDecl> {
    auto entry = tokens.begin();

    consume(Word, KWD_STRUCT);
    const string name = consume_word();
    if (!is_uword(name))
      throw std::runtime_error("Expected capitalized name for struct decl");

    auto type_args = vector<string>{};
    if (try_consume(Symbol, SYM_LT))
      consume_with_commas_until(Symbol, SYM_GT, [&] { type_args.push_back(consume_word()); });

    consume(Symbol, SYM_LPAREN);
    auto fields = vector<TypeName>{};
    consume_with_commas_until(Symbol, SYM_RPAREN, [&] { fields.push_back(std::move(*parse_type_name())); });

    auto body = vector<unique_ptr<Stmt>>{};
    auto body_begin = entry;

    require_separator();

    body_begin = tokens.begin();
    while (!try_consume(Word, KWD_END)) {
      body.push_back(parse_stmt());
      ignore_separator();
    }

    return ast_ptr<StructDecl>(entry, name, move(fields), type_args, make_ast<Compound>(body_begin, move(body)));
  }

  auto parse_fn_decl() -> unique_ptr<FnDecl> {
    auto entry = tokens.begin();

    consume(Word, KWD_DEF);
    const string name = parse_fn_name();
    auto type_args = vector<string>{};
    if (try_consume(Symbol, SYM_LT))
      consume_with_commas_until(Symbol, SYM_GT, [&] { type_args.push_back(consume_word()); });

    consume(Symbol, SYM_LPAREN);

    auto args = vector<TypeName>{};
    consume_with_commas_until(Symbol, SYM_RPAREN, [&] { args.push_back(std::move(*parse_type_name())); });

    auto ret_type = try_parse_type();
    auto body = vector<unique_ptr<Stmt>>{};
    auto body_begin = entry;

    if (try_consume(Symbol, SYM_EQ)) { // A "short" function definition, consists of a single expression
      if (try_consume(Word, KWD_PRIMITIVE)) {
        consume(Symbol, SYM_LPAREN);
        auto primitive = consume_word();
        consume(Symbol, SYM_RPAREN);
        auto varargs = try_consume(Word, KWD_VARARGS);
        return ast_ptr<FnDecl>(entry, name, move(args), type_args, move(ret_type), varargs, primitive);
      }
      body_begin = tokens.begin();
      auto expr = parse_expr();
      body.push_back(ast_ptr<ReturnStmt>(entry, move(expr)));
    } else {
      require_separator();

      body_begin = tokens.begin();
      while (!try_consume(Word, KWD_END)) {
        body.push_back(parse_stmt());
        ignore_separator();
      }
    }

    return ast_ptr<FnDecl>(entry, name, move(args), type_args, move(ret_type),
                           make_ast<Compound>(body_begin, move(body)));
  }

  auto parse_var_decl() -> unique_ptr<VarDecl> {
    auto entry = tokens.begin();

    consume(Word, KWD_LET);
    const string name = consume_word();
    auto type = try_parse_type();

    consume(Symbol, SYM_EQ);

    auto init = parse_expr();

    return ast_ptr<VarDecl>(entry, name, move(type), move(init));
  }

  auto parse_while_stmt() -> unique_ptr<WhileStmt> {
    auto entry = tokens.begin();

    consume(Word, KWD_WHILE);
    auto cond = parse_expr();

    ignore_separator();

    auto body_begin = tokens.begin();
    auto body = vector<unique_ptr<Stmt>>{};
    while (!try_consume(Word, KWD_END)) {
      body.push_back(parse_stmt());
      ignore_separator();
    }

    auto compound = make_ast<Compound>(body_begin, move(body));

    return ast_ptr<WhileStmt>(entry, move(cond), std::move(compound));
  }

  auto parse_return_stmt() -> unique_ptr<ReturnStmt> {
    auto entry = tokens.begin();

    consume(Word, KWD_RETURN);
    if (!tokens.at_end() && try_peek(0, Separator))
      return ast_ptr<ReturnStmt>(entry, optional<unique_ptr<Expr>>{});

    auto expr = parse_expr();

    return ast_ptr<ReturnStmt>(entry, optional<unique_ptr<Expr>>{move(expr)});
  }

  auto parse_if_stmt() -> unique_ptr<IfStmt> {
    auto entry = tokens.begin();
    auto clause_begin = entry;
    consume(Word, KWD_IF);
    auto cond = parse_expr();
    if (!try_consume(Word, KWD_THEN))
      require_separator();

    auto current_entry = tokens.begin();
    auto else_entry = tokens.begin();
    auto clauses = vector<IfClause>{};
    auto current_body = vector<unique_ptr<Stmt>>{};
    auto else_body = vector<unique_ptr<Stmt>>{};
    bool in_else = false;

    while (true) {
      auto current_clause_begin = tokens.begin();
      if (try_consume(Word, KWD_END))
        break;
      if (try_consume(Word, KWD_ELSE)) {
        // An `else` followed by an `if` begins a new clause of the same if statement.
        if (!in_else && try_consume(Word, KWD_IF)) {
          clauses.emplace_back(ts(clause_begin), move(cond), make_ast<Compound>(current_entry, move(current_body)));
          current_body = vector<unique_ptr<Stmt>>{};
          cond = parse_expr();
          current_entry = tokens.begin();
          clause_begin = current_clause_begin;
        } else {
          in_else = true;
          else_entry = tokens.begin();
        }
        if (!try_consume(Word, KWD_THEN))
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
    auto value = stoll(*next().m_payload->m_str);

    return ast_ptr<NumberExpr>({entry, 1}, value);
  }

  auto parse_string_expr() -> unique_ptr<StringExpr> {
    auto entry = tokens.begin();
    expect(Token::Type::Literal);
    auto value = *next().m_payload->m_str;

    return ast_ptr<StringExpr>({entry, 1}, value);
  }

  auto parse_char_expr() -> unique_ptr<CharExpr> {
    auto entry = tokens.begin();
    expect(Token::Type::Char);
    auto value = (*next().m_payload->m_str)[0];

    return ast_ptr<CharExpr>({entry, 1}, value);
  }

  auto parse_primary() -> unique_ptr<Expr> {
    auto entry = tokens.begin();
    if (try_consume(Symbol, SYM_LPAREN)) {
      auto val = parse_expr();
      consume(Symbol, SYM_RPAREN);
      return val;
    }

    if (tokens->m_type == Number)
      return parse_number_expr();
    if (tokens->m_type == Token::Type::Literal)
      return parse_string_expr();
    if (tokens->m_type == Token::Type::Char)
      return parse_char_expr();
    if (try_consume(Word, KWD_TRUE))
      return ast_ptr<BoolExpr>(entry, true);
    if (try_consume(Word, KWD_FALSE))
      return ast_ptr<BoolExpr>(entry, false);

    if (tokens->m_type == Word) {
      if (try_peek_uword(0)) {
        auto type = parse_type();
        if (try_consume(Symbol, SYM_LPAREN)) {
          auto call_args = vector<unique_ptr<Expr>>{};
          consume_with_commas_until(Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr()); });
          return ast_ptr<CtorExpr>(entry, move(type), move(call_args));
        }
        if (try_consume(Symbol, SYM_LBRACKET)) {
          auto slice_members = vector<unique_ptr<Expr>>{};
          consume_with_commas_until(Symbol, SYM_RBRACKET, [&] { slice_members.push_back(parse_expr()); });
          return ast_ptr<SliceExpr>(entry, move(type), move(slice_members));
        }

        if (auto* qual_type = llvm::dyn_cast<QualType>(type.get());
            qual_type != nullptr && qual_type->qualifier() == Qualifier::Slice) {
          // This isn't a slice type, it's an empty slice literal!
          // i.e. `I32[]` should be parsed as `I32[...]` (where the ... is just empty)
          auto slice_members = vector<unique_ptr<Expr>>{};
          return ast_ptr<SliceExpr>(entry, std::move(qual_type->direct_base()), move(slice_members));
        }

        throw std::runtime_error("Couldn't make an expression from here with a type");
      }
      auto name = consume_word();
      if (try_consume(Symbol, SYM_LPAREN)) {
        auto call_args = vector<unique_ptr<Expr>>{};
        consume_with_commas_until(Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr()); });
        return ast_ptr<CallExpr>(entry, name, move(call_args));
      }
      return ast_ptr<VarExpr>({entry, 1}, name);
    }
    throw std::runtime_error("Couldn't make an expression from here");
  }

  auto parse_receiver(unique_ptr<Expr> receiver, auto receiver_entry) -> unique_ptr<Expr> {
    auto entry = tokens.begin();
    if (try_consume(Symbol, SYM_DOT)) {
      auto name = consume_word();
      auto call_args = vector<unique_ptr<Expr>>{};
      call_args.push_back(move(receiver));
      if (try_consume(Symbol, SYM_LPAREN)) { // A call with a dot `a.b(...)`
        consume_with_commas_until(Symbol, SYM_RPAREN, [&] { call_args.push_back(parse_expr()); });
        auto call = ast_ptr<CallExpr>(entry + 1, name, move(call_args));
        return parse_receiver(move(call), receiver_entry);
      }
      if (try_consume(Symbol, SYM_EQ)) { // A setter `a.b = ...`
        auto value = parse_expr();
        call_args.push_back(move(value));
        auto call = ast_ptr<CallExpr>(entry + 1, name + '=', move(call_args));
        return parse_receiver(move(call), receiver_entry);
      }
      auto noarg_call = ast_ptr<CallExpr>(receiver_entry, name, move(call_args));
      return parse_receiver(move(noarg_call), receiver_entry);
    }
    if (try_consume(Symbol, SYM_EQ)) {
      auto value = parse_expr();
      auto assign = ast_ptr<AssignExpr>(receiver_entry, move(receiver), move(value));
      return parse_receiver(move(assign), receiver_entry);
    }
    if (try_consume(Symbol, SYM_LBRACKET)) {
      auto args = vector<unique_ptr<Expr>>{};
      args.push_back(move(receiver));
      args.push_back(parse_expr());
      consume(Symbol, SYM_RBRACKET);
      if (try_consume(Symbol, SYM_EQ)) {
        auto value = parse_expr();
        args.push_back(move(value));
        auto call = ast_ptr<CallExpr>(entry, "[]=", move(args));
        return parse_receiver(move(call), receiver_entry);
      }
      auto call = ast_ptr<CallExpr>(entry, "[]", move(args));
      return parse_receiver(move(call), receiver_entry);
    }
    if (try_consume(Symbol, SYM_COLON)) {
      consume(Symbol, SYM_COLON);
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
      if (try_consume(Symbol, un_op)) {
        auto value = parse_receiver();
        auto args = vector<unique_ptr<Expr>>{};
        args.push_back(move(value));
        return ast_ptr<CallExpr>(entry, un_op, move(args));
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
          if (try_consume(Symbol, op)) {
            auto right = parse_operator<N + 1>();
            auto args = vector<unique_ptr<Expr>>{};
            args.push_back(move(left));
            args.push_back(move(right));
            left = ast_ptr<CallExpr>(entry, op, move(args));
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

  if (tokens->is_keyword(KWD_DEF))
    stat = parse_fn_decl();
  else if (tokens->is_keyword(KWD_STRUCT))
    stat = parse_struct_decl();
  else if (tokens->is_keyword(KWD_LET))
    stat = parse_var_decl();
  else if (tokens->is_keyword(KWD_WHILE))
    stat = parse_while_stmt();
  else if (tokens->is_keyword(KWD_IF))
    stat = parse_if_stmt();
  else if (tokens->is_keyword(KWD_RETURN))
    stat = parse_return_stmt();
  else
    stat = parse_expr();

  require_separator();
  return stat;
}

auto Parser::parse_type(bool implicit_self) -> unique_ptr<Type> {
  auto entry = tokens.begin();
  auto base = [&]() -> unique_ptr<Type> {
    if (implicit_self || try_consume(Word, KWD_SELF_TYPE))
      return ast_ptr<SelfType>(entry);

    const string name = consume_word();
    if (!(is_uword(name)))
      throw std::runtime_error("Expected capitalized payload for simple type");

    return ast_ptr<SimpleType>(entry, name);
  }();
  while (true) {
    if (try_consume(Word, KWD_PTR)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ptr);
    } else if (try_consume(Word, KWD_MUT)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Mut);
    } else if (try_peek(0, Symbol, SYM_LBRACKET) && try_peek(1, Symbol, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(Symbol, SYM_LBRACKET);
      consume(Symbol, SYM_RBRACKET);
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Slice);
    } else if (try_consume(Symbol, SYM_LT)) {
      auto type_args = vector<string>{};
      consume_with_commas_until(Symbol, SYM_GT, [&] { type_args.push_back(consume_word()); });

      base = ast_ptr<TemplatedType>(entry, move(base), std::move(type_args));
    } else {
      break;
    }
  }

  return base;
}

auto Parser::try_parse_type() -> optional<unique_ptr<Type>> {
  auto entry = tokens.begin();
  if (tokens->m_type != Word || !tokens->m_payload.has_value())
    return {};

  const string name = consume_word();
  if (make_atom(name) != KWD_SELF_TYPE && !is_uword(name))
    return {};

  auto base = [&]() -> unique_ptr<Type> {
    if (make_atom(name) == KWD_SELF_TYPE)
      return ast_ptr<SelfType>(entry);

    return ast_ptr<SimpleType>(entry, name);
  }();

  while (true) {
    if (try_consume(Word, KWD_PTR)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Ptr);
    } else if (try_consume(Word, KWD_MUT)) {
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Mut);
    } else if (try_peek(0, Symbol, SYM_LBRACKET) && try_peek(1, Symbol, SYM_RBRACKET)) {
      // Don't consume the `[` unless the `]` is directly after; it might be a slice literal.
      consume(Symbol, SYM_LBRACKET);
      consume(Symbol, SYM_RBRACKET);
      base = ast_ptr<QualType>(entry, move(base), Qualifier::Slice);
    } else {
      break;
    }
  }

  return base;
}

auto Parser::parse_type_name() -> unique_ptr<TypeName> {
  auto entry = tokens.begin();
  if (try_consume(Word, KWD_SELF_ITEM)) {
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
