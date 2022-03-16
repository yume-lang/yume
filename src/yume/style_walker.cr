require "colorize"

class Yume::StyleWalker(*T)
  @last = 0

  def initialize(@input : Lexer(*T), @out : String::Builder)
    until (t = @input.token).nil?
      add(*t)
    end
  end

  def add(sym : Symbol, r, pos : Range(Int32, Int32))
    source = @input.source
    w = source[pos]
    @out << source[@last...pos.begin]
    @last = pos.end
    col = if sym.in?(:fn, :if, :then, :else, :do, :end, :while, :return, :__primitive__, :__varargs__)
            Colorize::Color256.new 172
          elsif sym.in?(:_str, :_int)
            Colorize::ColorANSI::Magenta
          elsif sym == :_uword
            Colorize::ColorANSI::LightBlue
          elsif sym.in?(:":=", :"=", :+, :-, :*, :"==", :%, :"//")
            Colorize::Color256.new 147
          elsif sym.in?(:|, :";")
            Colorize::ColorANSI::LightGray
          else
            Colorize::ColorANSI::White
          end
    @out << w.colorize col
  end
end
