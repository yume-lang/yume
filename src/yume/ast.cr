module Yume::AST
  # TODO: actually use positions
  record Pos, s : Int32, e : Int32 do
    def inspect(io : IO) : Nil
      io << "ChrPos["
      to_s io
      io << "]"
    end

    def to_s(io : IO) : Nil
      io << s << ":" << e
    end

    def self.join(i : Array(Pos)) : Pos
      s = i.min_of &.s
      e = i.max_of &.e
      Pos.new s, e
    end
  end

  # TODO: maybe remove completely, or at least add useful position functions
  module AST
    property! pos_parts : Hash(Symbol, Pos)

    def pos
      @pos_parts.nil? ? Pos.new(0, 0) : Pos.join(pos_parts.values)
    end

    def pretty_print(pp) : Nil
      {% if @type.ancestors.includes?(Reference) && @type.overrides?(Reference, "inspect") %}
        pp.text inspect
      {% elsif @type.ancestors.includes?(Struct) && @type.overrides?(Struct, "inspect") %}
        pp.text inspect
      {% else %}
        prefix = "#{"~".colorize.yellow}#{{{@type.name.split("AST::").last.id.stringify}}.colorize.yellow.dim}("
        {% if @type.ancestors.includes?(Reference) %}
        executed = exec_recursive(:pretty_print) do
        {% end %}
          pp.surround(prefix, ")", left_break: "", right_break: nil) do
            {% for ivar, i in @type.instance_vars.map(&.name).reject(&.== "pos_parts").sort %}
              {% if i > 0 %}
                pp.comma
              {% end %}
              %pos = @pos_parts.try &.[{{ivar.symbolize}}]?
              pp.group do
                pp.text "@#{{{ivar.id.stringify}}}#{".#{%pos || "?"}".colorize.dark_gray}="
                pp.nest do
                  pp.breakable ""
                  @{{ivar.id}}.pretty_print(pp)
                end
              end
            {% end %}
          end
        {% if @type.ancestors.includes?(Reference) %}
        end
        unless executed
          pp.text "#{prefix}...)"
        end
        {% end %}
      {% end %}
    end
  end

  # TODO: like, most of these should be records or something

  abstract class Expression
    include AST
  end

  class IntLiteral < Expression
    include AST
    getter val : Int64

    def initialize(@val : Int64)
    end
  end

  class VariableLiteral < Expression
    include AST
    getter name : String

    def initialize(@name)
    end
  end

  class StringLiteral < Expression
    include AST
    getter val : String

    def initialize(@val)
    end
  end

  class CharLiteral < Expression
    include AST
    getter val : String

    def initialize(@val)
    end
  end

  class ParenthesizedExpression < Expression
    include AST
    getter val : Expression

    def initialize(@val)
    end
  end

  class ArrayLiteral < Expression
    include AST
    getter val : Array(Expression)
    getter type : Type

    def initialize(@type, @val)
    end
  end

  struct FnName
    include AST
    getter name : String

    def initialize(@name : String)
    end
  end

  class Call < Expression
    include AST
    getter name : FnName
    getter args : Array(Expression)

    def initialize(@name, @args)
    end
  end

  abstract class Statement
    include AST
  end

  class ReturnStatement < Statement
    include AST
    getter expression : Expression

    def initialize(@expression)
    end
  end

  class ExpressionStatement < Statement
    include AST
    getter expression : Expression

    def initialize(@expression)
    end
  end

  class DeclarationStatement < Statement
    include AST
    getter name : TypedName
    getter value : Expression

    def initialize(@name, @value)
    end
  end

  class AssignmentStatement < Statement
    include AST
    getter name : String
    getter value : Expression

    def initialize(@name, @value)
    end
  end

  record ElseClause, statements : Array(Statement) do
    include AST
  end

  class IfStatement < Statement
    include AST
    getter condition : Expression
    getter statements : Array(Statement)
    getter else_clause : ElseClause?

    def initialize(@condition, @statements, @else_clause)
    end
  end

  class WhileStatement < Statement
    include AST
    getter condition : Expression
    getter statements : Array(Statement)

    def initialize(@condition, @statements)
    end
  end

  abstract class Type
    include AST
  end

  class PtrType < Type
    include AST
    getter type : Type

    def initialize(@type)
    end
  end

  class SliceType < Type
    include AST
    getter type : Type

    def initialize(@type)
    end
  end

  class SimpleType < Type
    include AST
    getter type : String

    def initialize(@type)
    end
  end

  record TypedName, type : Type, name : String do
    include AST
  end

  abstract class FunctionDefinition < Statement
    include AST

    abstract def decl : FunctionDeclaration
    def name : FnName
      decl.name
    end
    def args : Array(TypedName)
      decl.args.try(&.args) || [] of TypedName
    end
    def return_type : Type?
      decl.return_type
    end
  end

  struct GenericArgs
    include AST

    getter args : Array(Type)

    def initialize(@args)
    end
  end

  struct FnArgs
    include AST

    getter args : Array(TypedName)

    def initialize(@args)
    end
  end

  struct FunctionDeclaration
    include AST

    getter name : FnName
    getter return_type : Type?
    getter args : FnArgs
    getter generics : GenericArgs?

    def initialize(@name, @generics, @args, @return_type)
    end
  end

  class LongFunctionDefinition < FunctionDefinition
    include AST
    getter decl : FunctionDeclaration
    getter body : Array(Statement)

    def initialize(@decl, @body)
    end
  end

  class ShortFunctionDefinition < FunctionDefinition
    include AST
    getter decl : FunctionDeclaration
    getter expression : Expression

    def initialize(@decl, @expression)
    end
  end

  class PrimitiveDefinition < FunctionDefinition
    include AST
    getter decl : FunctionDeclaration
    getter primitive : String
    getter? varargs : Bool

    def initialize(@decl, @primitive, @varargs = false)
    end
  end

  class Program
    include AST
    getter statements : Array(Statement)

    def initialize(@statements)
    end
  end
end
