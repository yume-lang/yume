require "colorize"
require "json"
require "./yume/compiler"
require "./yume/style_walker"
require "./yume/ast"
require "./yume/parser"

module Yume
  VERSION = "0.1.0"

  private class DebugLexerConsumer
    def initialize(@io : IO)
    end

    def add(sym : Symbol, r = nil, *, pos : Parser::Pos)
      Colorize.with.cyan.surround @io do
        sym.inspect @io
      end
      if r
        @io << "("
        r.inspect @io
        @io << ")"
      end
      @io << " | ".colorize.dark_gray
    end

    def eof
      @io << "EOF".colorize.red
      @io.puts
    end
  end

  lexer = Lexer(String, Int64).build do
    match :fn, :end, :return, :if, :else, :while
    match :__primitive__, :__varargs__
    match :"(", :")", :"[", :"]", :"<", :">"
    match :"==", :+, :"%", :"//", :"/", :"*"
    match :"=", :",", :":"
    match /[a-z_][a-zA-Z0-9_]*/, :_word, &.[0]
    match /[A-Z][a-zA-Z0-9]*/, :_uword, &.[0]
    match /[0-9]+/, :_int, &.[0].to_i64
    match /"([^"]*)"/, :_str, &.[1]
    match /\?(.)/, :_chr, &.[1]
    match /[\n;]/, :sep
    match /$/, :eos
    skip /[^\S\r\n]/
  end

  input = File.open(ARGV[0]? || "example/test.ym", &.gets_to_end)

  {% if flag?("debug_lex") %}
    STDERR.puts
    debug = DebugLexerConsumer.new STDERR
    lexer.lex(input, debug)
    STDERR.puts
  {% end %}

  lexer.lex(input)
  puts String.build { |sb| StyleWalker.new lexer, sb }

  lexer.lex(input)
  program = Parser.new(lexer).parse_program

  {% if flag?("debug_ast") %}
    PrettyPrint.format(program, STDOUT, 139)
    puts "\n\n"
  {% end %}

  compiler = Compiler.new program
  File.open("out/out.ll", "w", &.puts(compiler.@mod.to_s))
  `llc out/out.ll -o out/out.s -O3`
  `clang out/out.s -static -o out/yume.out`
end
