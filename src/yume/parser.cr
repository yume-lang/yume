require "char/reader"
require "colorize"
require "./lexer"
require "./ast"

class Yume::Parser(*T)
  alias Standard = ::Yume::Parser(String, Int64)

  getter current_token : {Symbol, Union(*T)?, Range(Int32, Int32)}?
  @last_char = 0

  def initialize(@src : Lexer(*T))
    next_token
  end

  def pos
    @src.pos
  end

  def str
    @src.source
  end

  def debug_pos(range : Range(Int32, Int32)? = nil, col = :yellow, comment = nil)
    range = current_token.try(&.[2]) || (pos..pos) if range.nil?

    start_pos = range.begin
    end_pos = range.end
    end_pos -= 1 if range.excludes_end?
    end_pos = end_pos.clamp(0..(str.size - 1))

    highlight = str[start_pos..end_pos]
    special_char_adjust = highlight.starts_with?("\n") ? 1 : 0

    line_start = str.rindex("\n", offset: start_pos - special_char_adjust).try &.+ 1
    line_end = str.index("\n", offset: end_pos)
    line_no = str[...start_pos].count("\n") + 1

    prefix = str[line_start...start_pos]
    suffix = str[(end_pos + 1)...line_end]

    line = "#{prefix}#{highlight.inspect_unquoted.colorize(col).reverse}#{suffix}"
    line.split("\n").each_with_index do |i, j|
      puts if j > 0
      print "#{(line_no + j).to_s.rjust(4)} | #{i}"
    end
    if comment
      STDOUT << " // #{comment}".colorize.dim << "\n"
    else
      puts
    end
  end

  def positioned(& : -> T) : T forall T
    start_pos = pos
    ast_t = yield
    ast_t.at start_pos, pos
  end

  def positioned?(& : -> T?) : T? forall T
    start_pos = pos
    ast_t = yield
    ast_t ? ast_t.at(start_pos, pos) : ast_t
  end

  def parse_program : AST::Program
    statements = [] of AST::Statement
    until @src.eof?
      st = parse_statement
      statements << st if st
    end
    AST::Program.new(statements)
  end

  def parse_statement : AST::Statement?
    positioned? do
      if consume? :__primitive__
        consume :"("
        primitive_name = consume_val(:_word, String)
        consume :")"
        is_varargs = !!consume?(:__varargs__)
        decl = parse_fn_decl
        fn = AST::PrimitiveDefinition.new(
          decl: decl,
          primitive: primitive_name,
          varargs: is_varargs
        )
        consume_sep
        fn
      elsif consume? :fn
        decl = parse_fn_decl
        if consume?(:"=")
          expression = parse_expression
          consume_sep
          AST::ShortFunctionDefinition.new(decl, expression)
        else
          consume_sep
          body = [] of AST::Statement
          until consume?(:end)
            st = parse_statement
            body << st if st
          end
          consume_sep
          AST::LongFunctionDefinition.new decl, body
        end
      elsif consume? :while
        condition = parse_expression
        consume_sep
        body = [] of AST::Statement
        until consume?(:end)
          st = parse_statement
          body << st if st
        end
        consume_sep
        AST::WhileStatement.new condition, body
      elsif consume? :if
        condition = parse_expression
        unless consume? :then
          consume_sep
        end
        clauses = [] of AST::IfClause
        current_clause_body = [] of AST::Statement
        else_body = [] of AST::Statement
        in_else = false
        loop do
          if !in_else && consume? :else
            if consume? :if
              clauses << AST::IfClause.new condition, current_clause_body
              condition = parse_expression
              unless consume? :then
                consume_sep
              end
              current_clause_body = [] of AST::Statement
            else
              consume_sep
              in_else = true
            end
          end
          break if consume? :end
          st = parse_statement
          if st
            (in_else ? else_body : current_clause_body) << st
          end
        end
        clauses << AST::IfClause.new condition, current_clause_body
        else_clause = else_body.empty? ? nil : AST::ElseClause.new else_body
        consume_sep
        AST::IfStatement.new clauses, else_clause
      elsif consume? :return
        if consume? :sep
          AST::ReturnStatement.new nil
        else
          expression = parse_expression
          consume_sep
          AST::ReturnStatement.new expression
        end
      elsif consume? :struct
        name = consume_val :_uword, String
        consume_sep

        fields = [] of AST::TypedName
        body = [] of AST::Statement

        # TODO: make this less peek-heavy
        loop do
          field = peek? {
            t = parse_typed_name?
            t if t && consume_sep?
          }
          break if field.nil?
          fields << field
        end

        until consume?(:end)
          st = parse_statement
          body << st if st
        end
        consume_sep
        AST::StructDefinition.new(name, fields, body)
      elsif consume? :let
        name = consume_val :_word, String
        type = parse_type?
        if consume?(:"=")
          value = parse_expression
          consume_sep
          AST::DeclarationStatement.new type, name, value
        else
          raise "Uninitialized variable declaration not yet supported"
        end
      else
        expression = parse_expression
        if consume?(:"=")
          value = parse_expression
          consume_sep
          AST::AssignmentStatement.new expression, value
        else
          consume_sep
          AST::ExpressionStatement.new expression
        end
      end
    end
  end

  OPERATORS = [
    {:"==", :"!=", :">", :"<"},
    {:"+", :"-"},
    {:"%", :"//", :"*"},
  ]
  OPERATOR_FNS = OPERATORS.flat_map(&.each).map { |i| {i} } + [{:"[", :"]"}]

  def parse_expression : AST::Expression
    parse_op(0)
  end

  def parse_op(n = 0) : AST::Expression
    return parse_unary if n == OPERATORS.size
    left = parse_op(n + 1)
    loop do
      found_operator = false
      OPERATORS[n].each do |op|
        if consume?(op)
          right = parse_op(n + 1)
          left = AST::Call.new(
            AST::FnName.new(":" + op.to_s),
            [left, right]
          )
          found_operator = true
          break
        end
      end
      break unless found_operator
    end
    left
  end

  def parse_unary : AST::Expression
    if consume?(:"-")
      value = parse_receiver
      AST::Call.new(AST::FnName.new(":-"), [value])
    else
      parse_receiver
    end
  end

  def parse_receiver : AST::Expression
    parse_receiver(parse_primary)
  end

  def parse_receiver(receiver : AST::Expression) : AST::Expression
    if consume? :"."
      name = parse_fn_name
      if consume? :"("
        args = [receiver] of AST::Expression
        parse_call_args args
        parse_receiver AST::Call.new name, args
      else
        parse_receiver AST::Call.new name, [receiver] of AST::Expression
      end
    elsif consume? :"["
      name = AST::FnName.new ":[]"
      arg = parse_expression
      consume :"]"
      parse_receiver AST::Call.new name, [receiver, arg] of AST::Expression
    elsif consume? :":"
      consume :":"
      field = consume_val :_word, String
      parse_receiver AST::FieldAccess.new receiver, field
    else
      receiver
    end
  end

  def parse_call_args(args = [] of AST::Expression) : Array(AST::Expression)
    unless consume? :")"
      loop do
        args << parse_expression
        if consume? :")"
          break
        else
          consume :","
        end
      end
    end
    args
  end

  def parse_primary : AST::Expression
    positioned do
      if word = consume_val? :_word, String
        if consume? :"("
          args = parse_call_args
          AST::Call.new AST::FnName.new(word), args
        else
          AST::VariableLiteral.new word
        end
      elsif int = consume_val? :_int, Int64
        AST::IntLiteral.new int
      elsif str = consume_val? :_str, String
        AST::StringLiteral.new str
      elsif chr = consume_val? :_chr, String
        AST::CharLiteral.new chr
      elsif type = parse_type?
        if consume? :"("
          args = parse_call_args
          AST::CtorCall.new type, args
        elsif consume? :"["
          args = [] of AST::Expression
          unless consume? :"]"
            loop do
              args << parse_expression
              if consume? :"]"
                break
              else
                consume :","
              end
            end
          end
          AST::ArrayLiteral.new(type, args)
        else
          debug_pos col: :light_red
          raise "Couldn't find an expression"
        end
      else
        debug_pos col: :light_red
        raise "Couldn't find an expression"
      end
    end
  end

  def consume_sep? : Bool
    return false if @src.eof?
    if consume?(:sep)
      while consume?(:sep)
      end
      return true
    end
    return false
  end

  def consume_sep
    return if @src.eof?
    consume(:sep)
    while consume?(:sep)
    end
  end

  def parse_fn_decl : AST::FunctionDeclaration
    positioned do
      fn_external = false
      if consume?(:__extern__)
        fn_external = true
      end
      fn_name = parse_fn_name(allow_assignment: true)
      fn_generics = parse_fn_generics
      fn_args = parse_fn_args
      fn_return = parse_type?
      AST::FunctionDeclaration.new(fn_name, fn_generics, fn_args, fn_return, fn_external)
    end
  end

  def parse_operator_fn_name : String
    OPERATOR_FNS.each do |i|
      if consume?(i.first)
        i.each_with_index do |j, k|
          next if k.zero?
          consume j
        end
        return i.map(&.to_s).join ""
      end
    end
    debug_pos
    raise "Invalid operator fn, expected one of"
  end

  def parse_fn_name(allow_assignment = false) : AST::FnName
    positioned do
      if consume?(:":")
        name = parse_operator_fn_name
        AST::FnName.new(":" + name)
      else
        name = consume_val :_word, String
        if allow_assignment && consume?(:"=")
          name += "="
        end
        AST::FnName.new(name)
      end
    end
  end

  def parse_fn_generics : AST::GenericArgs?
    return nil unless consume?(:"<")
    positioned do
      args = [] of AST::Type
      loop do
        args << parse_type
        if consume?(:">")
          break
        else
          consume(:",")
        end
      end
      AST::GenericArgs.new(args)
    end
  end

  def parse_fn_args : AST::FnArgs
    positioned do
      consume :"("
      args = [] of AST::TypedName
      unless consume?(:")")
        loop do
          args << parse_typed_name
          if consume?(:")")
            break
          else
            consume(:",")
          end
        end
      end
      AST::FnArgs.new(args)
    end
  end

  def parse_typed_name : AST::TypedName
    positioned do
      name = consume_val :_word, String
      type = parse_type
      AST::TypedName.new(type, name)
    end
  end

  def parse_typed_name? : AST::TypedName?
    positioned? do
      name = consume_val? :_word, String
      if name
        type = parse_type?
        if type
          AST::TypedName.new(type, name)
        end
      end
    end
  end

  def parse_type : AST::Type
    positioned do
      if consume?(:self)
        type = AST::SelfType.new
      else
        type = AST::SimpleType.new consume_val :_uword, String
      end
      loop do
        if consume?(:"[")
          consume(:"]")
          type = AST::SliceType.new(type)
        elsif consume?(:"*")
          type = AST::PtrType.new(type)
        else
          break
        end
      end
      type
    end
  end

  def parse_type? : AST::Type?
    positioned? do
      if consume?(:self)
        type = AST::SelfType.new
      elsif uword = consume_val? :_uword, String
        type = AST::SimpleType.new(uword)
      else
        return nil
      end
      loop do
        if peek?(:"[") { peek?(:"]") }
          consume(:"[")
          consume(:"]")
          type = AST::SliceType.new(type)
        elsif consume?(:"*")
          type = AST::PtrType.new(type)
        else
          break
        end
      end
      type
    end
  end

  macro consume?(kwd)
    consume?({{ kwd }}, comment: "@#{{{ @def.name.stringify }}}:#{ __LINE__ }")
  end

  macro consume(kwd)
    consume({{ kwd }}, comment: "@#{{{ @def.name.stringify }}}:#{ __LINE__ }")
  end

  def consume(kwd : Symbol, *, comment = "")
    t = consume?(kwd, comment: comment, col: :magenta)
    unless t
      debug_pos col: :light_red
      raise "Expected #{kwd} here, but found #{@current_token.try(&.[0])}!"
    end
    t
  end

  def consume_val(sym : Symbol, type : T.class) : T forall T
    s = consume sym
    s[1].as T
  end

  def consume_val?(sym : Symbol, type : T.class) : T? forall T
    s = consume? sym
    s.try &.[1].as T
  end

  def consume?(kwd : Symbol, *, comment = "", col = :cyan)
    current = @current_token
    return nil if current.nil?
    if current[0] == kwd
      {% if flag?("debug_parse") %}
        debug_pos current[2], col, debug_token(current).colorize.bold.to_s + comment
      {% end %}
      next_token
      current
    else
      nil
    end
  end

  def peek?(&)
    current = @current_token
    return nil if current.nil?
    lexer_pos = @src.pos
    ret = yield @current_token
    if ret.nil?
      @src.pos = lexer_pos
      @current_token = current
    end
    ret
  end

  def peek?(kwd : Symbol, &)
    current = @current_token
    return nil if current.nil?
    if current[0] == kwd
      lexer_pos = @src.pos
      next_token
      ret = yield @current_token
      @src.pos = lexer_pos
      @current_token = current
      ret
    else
      nil
    end
  end

  def peek?(kwd : Symbol)
    peek?(kwd) { |i| i }
  end

  def debug_token(t)
    t[1].nil? ? "" : t[0].to_s
  end

  def current!
    current = @current_token.not_nil!
    {% if flag?("debug_parse") %}
      debug_pos current[2], :light_magenta, debug_token current
    {% end %}
    next_token
    current
  end

  def next_token
    @current_token = @src.token
    # debug_pos
    # @current_token
  end
end
