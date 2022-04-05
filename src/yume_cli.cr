require "colorize"
require "./yume"
require "./yume/debug_lexer_consumer"

module Yume
  # TODO: more proper include logic and stdlib/prelude logic
  source_files = [
    "#{ {{ __DIR__ }} }/../std/core.ym",
    ARGV[0]
  ]
  source_programs = source_files.map do |s|
    input = File.open(s, &.gets_to_end)

    puts s

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

    program
  end

  compiler = Compiler.new source_programs
  File.open("out/out.ll", "w", &.puts(compiler.@mod.to_s))
  `llc out/out.ll -o out/out.s -O3`
  `clang out/out.s -static -o out/yume.out`
end
