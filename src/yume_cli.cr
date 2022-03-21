require "colorize"
require "./yume"
require "./yume/debug_lexer_consumer"

module Yume
  input = File.open(ARGV[0]? || "example/test.ym", &.gets_to_end)

  {% if flag?("debug_lex") %}
    STDERR.puts
    debug = DebugLexerConsumer.new LEXER.lex(input), STDERR
    STDERR.puts
  {% end %}

  puts String.build { |sb| StyleWalker.new LEXER.lex(input), sb }

  program = Parser.new(LEXER.lex(input)).parse_program

  {% if flag?("debug_ast") %}
    PrettyPrint.format(program, STDOUT, 119)
    puts "\n\n"
  {% end %}

  compiler = Compiler.new program
  File.open("out/out.ll", "w", &.puts(compiler.@mod.to_s))
  `llc out/out.ll -o out/out.s -O3`
  `clang out/out.s -static -o out/yume.out`
end
