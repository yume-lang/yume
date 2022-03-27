module Yume::AST
  # TODO: maybe remove completely, or at least add useful position functions
  module AST
    alias Pos = Range(Int32, Int32)
    property! pos : Pos

    def at(pos : Pos) : self
      @pos = pos
      self
    end

    def at(s : Int32, e : Int32) : self
      at(s..e)
    end

    def pretty_print(pp) : Nil
      {% if @type.ancestors.includes?(Reference) && @type.overrides?(Reference, "inspect") %}
        pp.text inspect
      {% elsif @type.ancestors.includes?(Struct) && @type.overrides?(Struct, "inspect") %}
        pp.text inspect
      {% else %}
        %pos = @pos
        prefix = "#{%pos.nil? ? "" : "#{%pos.to_s.colorize.dark_gray}:"}#{{{@type.name.split("AST::").last.id.stringify}}.colorize.yellow.dim}("
        {% if @type.ancestors.includes?(Reference) %}
        executed = exec_recursive(:pretty_print) do
        {% end %}
          pp.surround(prefix, ")", left_break: "", right_break: nil) do
            {% ivars = @type.instance_vars.map(&.name).reject(&.== "pos").sort %}
            {% if ivars.size == 1 %}
              pp.group do
                @{{ivars[0].id}}.pretty_print(pp)
              end
            {% else %}
              {% for ivar, i in ivars %}
                {% if i > 0 %}
                  pp.comma
                {% end %}
                pp.group do
                  pp.text "@#{{{ivar.id.stringify}}}="
                  pp.nest do
                    pp.breakable ""
                    @{{ivar.id}}.pretty_print(pp)
                  end
                end
              {% end %}
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

  macro ast_record(name, *properties)
    class {{name.id}}
      include AST
      {% prop_names = [] of Nil %}
      {% for property in properties %}
        {% if property.is_a?(Assign) %}
          getter {{property.target.id}}
          {% prop_names << property.target.id %}
        {% elsif property.is_a?(TypeDeclaration) %}
          getter {{ property }}
          {% prop_names << property.var.id %}
        {% else %}
          getter :{{property.id}}
          {% prop_names << property %}
        {% end %}
      {% end %}

      def initialize({{ *properties.map { |field| "@#{field.id}".id } }})
      end

      {{yield}}

      def_equals_and_hash {{ *prop_names.map { |field| "@#{field.id}".id } }}
    end
  end

  # TODO: like, most of these should be records or something

  abstract class Expression
    include AST
  end

  ast_record IntLiteral < Expression, val : Int64

  ast_record VariableLiteral < Expression, name : String

  ast_record StringLiteral < Expression, val : String

  ast_record CharLiteral < Expression, val : String

  class ParenthesizedExpression < Expression
    include AST
    getter val : Expression

    def initialize(@val)
    end

    def_equals_and_hash @val
  end

  class ArrayLiteral < Expression
    include AST
    getter val : Array(Expression)
    getter type : Type

    def initialize(@type, @val)
    end

    def_equals_and_hash @type, @val
  end

  struct FnName
    include AST
    getter name : String

    def initialize(@name : String)
    end

    def_equals_and_hash @name
  end

  class Call < Expression
    include AST
    getter name : FnName
    getter args : Array(Expression)

    def initialize(@name, @args)
    end

    def_equals_and_hash @name, @args
  end

  ast_record CtorCall < Expression, type : Type, args : Array(Expression)

  ast_record FieldAccess < Expression, base : Expression, field : String

  abstract class Statement
    include AST
  end

  class ReturnStatement < Statement
    include AST
    getter expression : Expression?

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

  record IfClause, condition : Expression, body : Array(Statement) do
    include AST
  end

  class IfStatement < Statement
    include AST
    getter clauses : Array(IfClause)
    getter else_clause : ElseClause?

    def initialize(@clauses, @else_clause)
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

  ast_record StructDefinition < Statement, name : String, fields : Array(TypedName), body : Array(Statement)

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
    getter? external : Bool

    def initialize(@name, @generics, @args, @return_type, @external = false)
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
