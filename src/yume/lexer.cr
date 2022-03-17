class Yume::Lexer(*T)
  @matches = Array({Regex, Symbol, Proc(Regex::MatchData, Union(*T))?}).new
  @skips = Array(Regex).new
  getter! source : String
  getter? eof : Bool = false
  property pos = 0

  def self.build(&) : self
    ins = new
    with ins yield
    ins
  end

  def match(regex : Regex, result : Symbol)
    @matches << {regex, result, nil}
  end

  def match(*symbols : Symbol)
    symbols.each do |symbol|
      @matches << {Regex.new(Regex.escape symbol.to_s), symbol, nil}
    end
  end

  macro keywords(*kws)
    {% for kw in kws %}
      match(Regex.new(Regex.escape({{kw}}.to_s)), {{"kw_#{kw.id}".id.symbolize}})
    {% end %}
  end

  def match(regex : Regex, symbol : Symbol, &result : Regex::MatchData -> Union(*T))
    @matches << {regex, symbol, result}
  end

  def skip(regex : Regex)
    @skips << regex
  end

  def lex(input : String)
    @source = input
    @pos = 0
    @eof = false
  end

  def token : {Symbol, Union(*T)?, Range(Int32, Int32)}?
    ret = nil
    while true
      s_pos = pos
      found = false
      next if @skips.any? do |m|
                if match = m.match(source, pos, Regex::Options::ANCHORED)
                  @pos = match.end
                  true
                end
              end
      @matches.each do |m, sym, r|
        if match = m.match(source, pos, Regex::Options::ANCHORED)
          r = r.call match if r.is_a? Proc
          ret = {sym, r, (match.begin...match.end)}
          @pos = match.end
          found = true
          break
        end
      end

      if pos == s_pos
        unless found
          raise "no match at pos #{pos}: `#{source[pos..pos + 30].dump_unquoted}`..."
        end
        @eof = true
        return nil
      end
      break if found
      break if pos == source.size
    end
    @eof = ret.nil?
    return ret
  end
end
