require "llvm"
require "./ast"

class Yume::Compiler
  abstract struct Type
    abstract def to_llvm(ctx : LLVM::Context) : LLVM::Type
    def to_llvm(ctx : LLVM::Context, generic_args : Hash(String, Type)) : LLVM::Type
      to_llvm(ctx)
    end

    INT_32     = IntegralType.signed 32
    UINT_8     = IntegralType.unsigned 8
    UINT_8_PTR = PointerType.new(UINT_8)
    BOOL       = PrimitiveBoolType.new
  end

  abstract struct PrimitiveType < Type
  end

  struct PrimitiveBoolType < PrimitiveType
    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      ctx.int1
    end

    def to_s(io : IO)
      io << ".Bool"
    end
  end

  struct PrimitiveVoidType < PrimitiveType
    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      ctx.void
    end
  end

  struct PointerType < Type
    @value : Pointer(Type)
    def_equals_and_hash value

    def initialize(val : Type)
      @value = Pointer(Type).malloc 1
      @value.value = val.as Type
    end

    def value : Type
      @value.value
    end

    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      value.to_llvm(ctx).pointer
    end

    def to_s(io : IO)
      value.to_s io
      io << "*"
    end
  end

  struct SliceType < Type
    @value : Pointer(Type)
    def_equals_and_hash value

    def initialize(val : Type)
      @value = Pointer(Type).malloc 1
      @value.value = val.as Type
    end

    def value : Type
      @value.value
    end

    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      ctx.struct([value.to_llvm(ctx).pointer, ctx.int32])
    end

    def to_s(io : IO)
      value.to_s io
      io << "[]"
    end
  end

  struct IntegralType < PrimitiveType
    getter size : Int32
    getter signed : Bool

    def initialize(@size, @signed)
    end

    def self.signed(size)
      new(size, true)
    end

    def self.unsigned(size)
      new(size, false)
    end

    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      case @size
      when  8 then ctx.int8
      when 32 then ctx.int32
      else         raise "Invalid int size #{@size}"
      end
    end

    def to_s(io : IO)
      io << "."
      io << (@signed ? "I" : "U")
      io << @size
    end
  end

  struct GenericType < Type
    getter name : String

    def initialize(@name)
    end

    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      raise "Can't emit a generic type (#{@name})!"
    end

    def to_llvm(ctx : LLVM::Context, generic_args : Hash(String, Type)) : LLVM::Type
      generic_args[@name].to_llvm(ctx, generic_args)
    end

    def to_s(io : IO)
      io << "<" << @name << ">"
    end
  end

  record Value, llvm : LLVM::Value, type : Type do
    def to_unsafe
      @llvm.to_unsafe
    end
  end

  @ctx : LLVM::Context
  @mod : LLVM::Module
  @builder : LLVM::Builder
  @libc = Hash(String, LLVM::Function).new
  @fns = Hash(AST::FunctionDefinition, LLVM::Function?).new
  @types = Hash(String, Type).new
  getter! cur_ast_fn : AST::FunctionDefinition?
  getter! cur_llvm_fn : LLVM::Function?
  @fn_scope = Hash(String, Value).new
  @type_scope = Hash(String, Type).new
  @instantiation_queue = Deque({AST::FunctionDefinition, LLVM::Function, Hash(String, Type)?}).new

  def initialize(@program : AST::Program)
    @ctx = LLVM::Context.new
    @mod = @ctx.new_module("yume")
    @builder = @ctx.new_builder
    add_libc_functions

    @types["I32"] = Type::INT_32
    @types["U8"] = Type::UINT_8
    @types["Bool"] = Type::BOOL

    @program.statements.each do |s|
      case s
      when AST::FunctionDefinition
        f = declare(s)
        next if f.nil?
        if s.name.name == "main" && s.args.empty?
          f.name = "main"
          f.linkage = LLVM::Linkage::External
        else
          f.linkage = LLVM::Linkage::Internal
        end
      end
    end
    @fns.each do |k, v|
      next if v.nil?
      @instantiation_queue << {k, v, nil}
    end

    until @instantiation_queue.empty?
      k, v, gen = @instantiation_queue.pop
      @type_scope = gen if gen
      instantiate(k, v)
      @type_scope.clear
    end

    {% if flag?("optimize") %}
      optimize @mod
    {% end %}
  end

  def instantiate(k : AST::FunctionDefinition, v : LLVM::Function)
    bb = v.basic_blocks.append "entry"
    @builder.position_at_end bb
    @fn_scope.clear
    @cur_ast_fn = k
    @cur_llvm_fn = v
    k.args.each_with_index do |a, i|
      type = resolve_type a.type
      lv = @fn_scope[a.name] = Value.new(@builder.alloca(llvm_type(type), a.name), type)
      @builder.store(v.params[i], lv.llvm)
    end
    case k
    in AST::LongFunctionDefinition
      compile_body k.body
    in AST::ShortFunctionDefinition
      @builder.ret expression(k.expression)
    in AST::FunctionDefinition
      raise "Unknown function kind #{k}"
    end
  end

  def resolve_generic(type : AST::Type, fn_def : AST::FunctionDefinition?) : Type?
    if inst = @type_scope[type.type]?
      return inst
    end
    return nil if fn_def.nil?
    if gen = fn_def.decl.generics
      if gen.args.any?(&.type.== type.type)
        GenericType.new(type.type)
      end
    end
  end

  def resolve_type(type : AST::Type?, fn_def : AST::FunctionDefinition? = nil) : Type
    return PrimitiveVoidType.new.as(Type) if type.nil?
    case type
    in AST::PtrType then PointerType.new(resolve_type(type.type, fn_def))
    in AST::SliceType then SliceType.new(resolve_type(type.type, fn_def))
    in AST::SimpleType then resolve_generic(type, fn_def) || @types[type.type]
    in AST::Type then raise "Invalid type #{type}"
    end
  end

  def llvm_type(type : Type) : LLVM::Type
    type.to_llvm(@ctx, @type_scope)
  end

  protected def optimize(llvm_mod)
    registry = LLVM::PassRegistry.instance
    registry.initialize_all
    pass_manager_builder = LLVM::PassManagerBuilder.new
    pass_manager_builder.opt_level = 3
    pass_manager_builder.size_level = 0
    pass_manager_builder.use_inliner_with_threshold = 275
    module_pass_manager = LLVM::ModulePassManager.new
    pass_manager_builder.populate module_pass_manager
    fun_pass_manager = llvm_mod.new_function_pass_manager
    pass_manager_builder.populate fun_pass_manager
    fun_pass_manager.run llvm_mod
    module_pass_manager.run llvm_mod
  end

  # TODO: use positions again when they work
  private def def_pos(m : AST::FunctionDefinition?)
    ":#{m.try &.pos.s.to_s}".rjust 5
  end

  def type_compatibility(a : Type, b : Type) : Int32
    if a == b
      return 2
    else
      if b.is_a?(GenericType)
        return 1
      elsif b.is_a?(SliceType)
        if a.is_a?(SliceType)
          return 1
        else
          return 0
        end
      else
        return 0
      end
    end
  end

  def expression(ex : AST::Expression) : Value
    case ex
    when AST::ParenthesizedExpression
      expression(ex.val)
    when AST::IntLiteral
      if ex.val < Int32::MAX && ex.val > Int32::MIN
        Value.new(@ctx.int32.const_int(ex.val.to_i32), Type::INT_32)
      else
        raise OverflowError.new
      end
    when AST::CharLiteral
      Value.new(@ctx.int8.const_int(ex.val[0].ord), Type::UINT_8)
    when AST::VariableLiteral
      var = @fn_scope[ex.name]
      Value.new(@builder.load(var.llvm), var.type)
    when AST::StringLiteral
      str = ex.val.gsub("\\n", "\n") # HACK
      str_val = @ctx.const_string str
      s = @mod.globals.add(str_val.type, "str:" + str)
      s.global_constant = true
      s.linkage = LLVM::Linkage::Internal
      s.initializer = str_val
      Value.new(@builder.bit_cast(s, @ctx.int8.pointer), Type::UINT_8_PTR)
    when AST::ArrayLiteral
      args = ex.val.map { |i| expression i }
      val_type = resolve_type(ex.type)
      arr_val = llvm_type(val_type).const_array args.map(&.llvm)
      s = @mod.globals.add(llvm_type(val_type).array(args.size), "array")
      s.global_constant = true
      s.initializer = arr_val
      s.linkage = LLVM::Linkage::Internal
      array = @builder.bit_cast(s, llvm_type(PointerType.new(val_type)))
      slice = @ctx.const_struct([array, @ctx.int32.const_int(args.size)])
      Value.new(slice, SliceType.new(val_type))
    when AST::Call
      overload_set = @fns.keys.select(&.name.name.== ex.name.name)

      args = ex.args.map { |i| expression i }
      print "Calling `#{ex.name.name}` with args:\n        "
      args.map(&.type).join STDOUT, ", "
      puts "\nOverloads:"

      overload_set.map { |i| {i, type_signature(i)} }.each do |m, (i, r)|
        print "#{def_pos m} | "
        i.join STDOUT, ", "
        print ", ..." if m.is_a?(AST::PrimitiveDefinition) && m.varargs?
        print " -> "
        puts r
      end

      # TODO: determine generics
      matching = overload_set.compact_map do |o|
        sig = type_signature(o)[0]
        next nil if sig.size > args.size
        valid = true
        compatibility = 0
        args.each_with_index do |a, i|
          if i >= sig.size
            break valid = (o.is_a?(AST::PrimitiveDefinition) && o.varargs?)
          end
          c = type_compatibility(a.type, sig[i])
          compatibility += c
          break valid = false if c.zero?
        end
        valid ? {o, compatibility} : nil
      end

      matching_fn_def = matching.max_by(&.[1])[0]
      puts "#{def_pos matching_fn_def} > Selected (#{matching.size})"
      matching_fn = @fns[matching_fn_def]
      puts

      if matching_fn.nil? && matching_fn_def.is_a? AST::PrimitiveDefinition
        known_type : Type? = nil
        v = case matching_fn_def.primitive
            when "libc"    then @builder.call(@libc[ex.name.name], args.map(&.llvm))
            when "add"     then @builder.add(args[0], args[1])
            when "mul"     then @builder.mul(args[0], args[1])
            when "mod"     then signed_type?(args[0].type) ? @builder.srem(args[0], args[1]) : @builder.urem(args[0], args[1])
            when "int_div" then signed_type?(args[0].type) ? @builder.sdiv(args[0], args[1]) : @builder.udiv(args[0], args[1])
            when "icmp_eq" then @builder.icmp(LLVM::IntPredicate::EQ, args[0], args[1])
            when "icmp_gt" then @builder.icmp((signed_type?(args[0].type) ? LLVM::IntPredicate::SGT : LLVM::IntPredicate::UGT), args[0], args[1])
            when "slice_size" then @builder.extract_value(args[0], 1)
            when "slice_ptr"
              known_type = PointerType.new args[0].type
              @builder.extract_value(args[0], 0)
            else                raise "unknown primitive #{matching_fn_def.primitive}"
            end
        Value.new(v, known_type || resolve_type matching_fn_def.return_type)
      else
        if matching_fn.nil?
          matching_fn = declare(matching_fn_def, {"T" => args[0].type}).not_nil!
          # instantiate(matching_fn_def, matching_fn)
          @instantiation_queue << {matching_fn_def, matching_fn, @type_scope.dup}
        end
        val = Value.new(@builder.call(matching_fn, args.map(&.llvm)), resolve_type(matching_fn_def.not_nil!.return_type))
        @type_scope.clear
        val
      end
    else
      STDERR.puts "Unknown expression #{ex}"
      Value.new(llvm_type(resolve_type(cur_ast_fn.return_type)).null, resolve_type cur_ast_fn.return_type)
      # raise "Unknown expression #{ex}"
    end
  end

  def statement(st : AST::Statement)
    case st
    when AST::ReturnStatement
      # TODO: multiple returns
      if expr = st.expression
        @builder.ret expression(expr)
      else
        @builder.ret
      end
    when AST::DeclarationStatement
      raise "Duplicate declaration #{st}" if @fn_scope.has_key?(st.name.name)
      type = resolve_type st.name.type
      lv = @fn_scope[st.name.name] = Value.new(@builder.alloca(llvm_type(type), st.name.name), type)
      @builder.store(expression(st.value), lv.llvm)
    when AST::AssignmentStatement
      raise "Not declared #{st}" unless @fn_scope.has_key?(st.name)
      @builder.store(expression(st.value), @fn_scope[st.name].llvm)
    when AST::ExpressionStatement
      expression(st.expression)
    when AST::IfStatement
      condition = expression(st.condition)
      then_bb = cur_llvm_fn.basic_blocks.append("if.then")
      else_bb = cur_llvm_fn.basic_blocks.append("if.else")
      merge_bb = cur_llvm_fn.basic_blocks.append("if.cont")
      @builder.cond(condition, then_bb, else_bb)
      @builder.position_at_end then_bb
      compile_body(st.statements)
      @builder.br(merge_bb)
      @builder.position_at_end else_bb
      if else_clause = st.else_clause
        compile_body(else_clause.statements)
      end
      @builder.br(merge_bb)
      @builder.position_at_end merge_bb
    when AST::WhileStatement
      test_bb = cur_llvm_fn.basic_blocks.append("while.test")
      head_bb = cur_llvm_fn.basic_blocks.append("while.head")
      merge_bb = cur_llvm_fn.basic_blocks.append("while.cont")
      @builder.br(test_bb)
      @builder.position_at_end test_bb
      condition = expression(st.condition)
      @builder.cond(condition, head_bb, merge_bb)
      @builder.position_at_end head_bb
      compile_body(st.statements)
      @builder.br(test_bb)
      @builder.position_at_end merge_bb
    else
      STDERR.puts "Unknown statement #{st}"
    end
  end

  def type_signature(fn : AST::FunctionDefinition) : {Slice(Type), Type}
    args = Slice(Type).new(fn.args.size) do |i|
      resolve_type(fn.args[i].type, fn)
    end
    {args, resolve_type(fn.return_type, fn)}
  end

  def compile_body(sts : Array(AST::Statement))
    sts.each do |st|
      statement st
    end
  end

  def signed_type?(t : Type) : Bool
    raise "Can't check signedness of non-integral type" unless t.is_a? IntegralType
    t.signed
  end

  def declare(fn : AST::FunctionDefinition) : LLVM::Function?
    declare(fn, {} of String => Type)
  end

  def declare(fn : AST::FunctionDefinition, generic_args : Hash(String, Type)) : LLVM::Function?
    if fn.decl.generics && generic_args.empty?
      @fns[fn] = nil
    else
      @type_scope = generic_args
      arg_types = fn.args.map { |i| llvm_type(resolve_type(i.type, fn)) }
      if fn.is_a? AST::PrimitiveDefinition
        f = nil
      else
        llvm_name = "ym:#{fn.name.name}(#{fn.args.map { |i| resolve_type(i.type).to_s }.join ","})"
        f = @mod.functions.add llvm_name, arg_types, llvm_type(resolve_type(fn.return_type, fn))
      end
      @fns[fn] = f
    end
  end

  def add_libc_functions
    @libc["puts"] = @mod.functions.add("puts", [@ctx.int8.pointer], @ctx.int32)
    @libc["printf"] = @mod.functions.add("printf", [@ctx.int8.pointer], @ctx.int32, varargs: true)
    @libc["donothing"] = @mod.functions.add("llvm.donothing", [] of LLVM::Type, @ctx.void)
  end
end
