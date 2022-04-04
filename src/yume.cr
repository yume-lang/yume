require "./yume/compiler"
require "./yume/style_walker"
require "./yume/ast"
require "./yume/parser"

module Yume
  VERSION = "0.1.0"

  LEXER = Lexer(String, Int64).build do
    match :fn, :let, :end, :return, :if, :else, :while, :struct
    match :__primitive__, :__varargs__, :__extern__
    match :"(", :")", :"[", :"]", :"<", :">"
    match /-?[0-9]+/, :_int, &.[0].to_i64
    match :"==", :"!=", :+, :"-", :"%", :"//", :"/", :"*"
    match :"=", :",", :":", :"."
    match /[a-z_][a-zA-Z0-9_]*/, :_word, &.[0]
    match /[A-Z][a-zA-Z0-9]*/, :_uword, &.[0]
    match /"([^"]*)"/, :_str, &.[1]
    match /\?(\\.|[^\\])/, :_chr, &.[1]
    match /[\n;]/, :sep
    match /$/, :eos
    skip /[^\S\r\n]/
    skip /#.*(?=\R)/
  end
end
