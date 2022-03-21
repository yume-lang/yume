require "colorize"
require "./yume"
require "./yume/debug_lexer_consumer"

module Yume
  lexer = LEXER
  input = File.open(ARGV[0]? || "example/test.ym", &.gets_to_end)

  {% if flag?("debug_lex") %}
    STDERR.puts
    lexer.lex(input)
    debug = DebugLexerConsumer.new lexer, STDERR
    STDERR.puts
  {% end %}

  lexer.lex(input)
  puts String.build { |sb| StyleWalker.new lexer, sb }

  lexer.lex(input)
  program = Parser.new(lexer).parse_program

  {% if flag?("debug_ast") %}
    PrettyPrint.format(program, STDOUT, 119)
    puts "\n\n"
  {% end %}

  compiler = Compiler.new program
  File.open("out/out.ll", "w", &.puts(compiler.@mod.to_s))
  `llc out/out.ll -o out/out.s -O3`
  `clang out/out.s -static -o out/yume.out`
end
