require "spec"
require "../src/yume"

def parser_for(src : String) : Yume::Parser::Standard
  Yume::Parser.new(Yume::LEXER.lex(src))
end
