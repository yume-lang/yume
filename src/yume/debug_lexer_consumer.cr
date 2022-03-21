module Yume
  class DebugLexerConsumer(*T)
    def initialize(@input : Lexer(*T), @io : IO)
      until (t = @input.token).nil?
        add(*t)
      end
    end

    def add(sym : Symbol, r, pos : Range(Int32, Int32))
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
  end
end
