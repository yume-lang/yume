require "colorize"
require "./yume"
require "./yume/debug_lexer_consumer"

module Yume
  # TODO: more proper include logic and stdlib/prelude logic
  source_files = [
    "#{ {{ __DIR__ }} }/../std/core.ym",
    ARGV[0]
  ]
  source_line_breaks = [] of Array(Int32)
  source_programs = source_files.map_with_index do |s, si|
    input = File.open(s, &.gets_to_end)

    line_breaks = [] of Int32
    index = 0
    while index = input.byte_index("\n", index)
      line_breaks << index
      index += 1
    end
    source_line_breaks << line_breaks

    puts "# File #{si} : #{s}"

    {% if flag?("debug_lex") %}
      STDERR.puts
      debug = DebugLexerConsumer.new LEXER.lex(input), STDERR
      STDERR.puts
    {% end %}

    puts String.build { |sb| StyleWalker.new LEXER.lex(input), sb }

    program = Parser.new(LEXER.lex(input), si).parse_program

    {% if flag?("debug_ast") %}
      PrettyPrint.format(program, STDOUT, 119)
      puts "\n\n"
    {% end %}

    program
  end

  compiler = Compiler.new source_programs, source_files, source_line_breaks
  File.open("out/out.ll", "w", &.puts(compiler.@mod.to_s))
  `llc out/out.ll -o out/out.s -O3`
  `clang out/out.s -static -o out/yume.out`
end
