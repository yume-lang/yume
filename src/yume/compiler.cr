require "llvm"
require "./ast"

class Yume::Compiler
  abstract struct Type
    abstract def to_llvm(ctx : LLVM::Context) : LLVM::Type

    def to_llvm(ctx : LLVM::Context, generic_args : Hash(String, Type)) : LLVM::Type
      to_llvm(ctx)
    end

    def to_llvm_value_type(ctx : LLVM::Context) : LLVM::Type
      to_llvm(ctx)
    end

    def to_llvm_value_type(ctx : LLVM::Context, generic_args : Hash(String, Type)) : LLVM::Type
      to_llvm_value_type(ctx)
    end

    def ptr : PointerType
      PointerType.new self
    end

    INT_8   = IntegralType.signed 8
    INT_16  = IntegralType.signed 16
    INT_32  = IntegralType.signed 32
    INT_64  = IntegralType.signed 64
    UINT_8  = IntegralType.unsigned 8
    UINT_16 = IntegralType.unsigned 16
    UINT_32 = IntegralType.unsigned 32
    UINT_64 = IntegralType.unsigned 64
    BOOL    = PrimitiveBoolType.new
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

    def to_s(io : IO)
      io << "void"
    end
  end

  struct PointerType < Type
    @value : Pointer(Type)

    def_equals_and_hash @value.value

    # HACK: This shouldn't be required...
    def ==(other)
      false
    end

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
    def_equals_and_hash @value.value

    # HACK: This shouldn't be required...
    def ==(other)
      false
    end

    def initialize(val : Type)
      @value = Pointer(Type).malloc 1
      @value.value = val.as Type
    end

    def value : Type
      @value.value
    end

    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      ctx.struct([value.to_llvm(ctx).pointer, ctx.int64])
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
      when 16 then ctx.int16
      when 32 then ctx.int32
      when 64 then ctx.int64
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

  struct StructType < Type
    getter name : String
    getter fields : Array(TypedName)
    @llvm_struct : LLVM::Type**

    def initialize(@name, @fields)
      @llvm_struct = Pointer(Pointer(LLVM::Type)).malloc
      @llvm_struct.value = Pointer(LLVM::Type).null
    end

    def to_llvm(ctx : LLVM::Context) : LLVM::Type
      if @llvm_struct.value.null?
        @llvm_struct.value = Pointer(LLVM::Type).malloc
        @llvm_struct.value.value = ctx.struct(@fields.map(&.type.to_llvm ctx), @name).pointer
      end
      @llvm_struct.value.value
    end

    def to_llvm_value_type(ctx : LLVM::Context) : LLVM::Type
      to_llvm(ctx).element_type
    end

    def to_s(io : IO)
      io << "." << @name
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
  @fn_self_types = Hash(AST::FunctionDefinition, Type).new
  getter! cur_ast_fn : AST::FunctionDefinition?
  getter! cur_llvm_fn : LLVM::Function?
  @fn_scope = Hash(String, Value).new
  @type_scope = Hash(String, Type).new
  @instantiation_queue = Deque({AST::FunctionDefinition, LLVM::Function, Hash(String, Type)?}).new
  @terminated = false

  def declare_body(body : Array(AST::Statement))
    body.each do |s|
      case s
      when AST::FunctionDefinition
        if self_type = @type_scope[""]?
          @fn_self_types[s] = self_type
        end
        f = declare(s)
        next if f.nil?
      when AST::StructDefinition
        @types[s.name] = @type_scope[""] = StructType.new(s.name, s.fields.map { |i| resolve_type i })
        declare_body s.body
      end
    end
  end

  def initialize(programs : Array(AST::Program))
    @ctx = LLVM::Context.new
    @mod = @ctx.new_module("yume")
    @builder = @ctx.new_builder
    add_libc_functions

    {8, 16, 32, 64}.each do |size|
      @types["I#{size}"] = IntegralType.signed size
      @types["U#{size}"] = IntegralType.unsigned size
    end
    @types["Bool"] = Type::BOOL

    programs.each do |program|
      declare_body program.statements
    end

    until @instantiation_queue.empty?
      k, v, gen = @instantiation_queue.pop
      if (k.name.name == "main" && k.args.empty?) || (k.decl.external?)
        v.name = k.name.name
        v.linkage = LLVM::Linkage::External
      else
        v.linkage = LLVM::Linkage::Internal
      end
      @type_scope = gen if gen
      instantiate(k, v)
      @type_scope.clear
    end

    {% if flag?("optimize") %}
      optimize @mod
    {% end %}
  end

  def instantiate(k : AST::FunctionDefinition, v : LLVM::Function)
    decl_bb = v.basic_blocks.append "decl"
    @builder.position_at_end decl_bb
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
      entry_bb = v.basic_blocks.append "entry"
      @builder.position_at_end entry_bb

      compile_body k.body

      unless @terminated
        @builder.ret
      end

      @builder.position_at_end decl_bb
      @builder.br entry_bb
    in AST::ShortFunctionDefinition
      @builder.ret expression k.expression
    in AST::FunctionDefinition
      raise "Unknown function kind #{k}"
    end
  end

  def resolve_generic(type : AST::Type, fn_def : AST::FunctionDefinition?, use_type_scope = true) : Type?
    if use_type_scope && (inst = @type_scope[type.type]?)
      return inst
    end
    return nil if fn_def.nil?
    if gen = fn_def.decl.generics
      if gen.args.any? { |i| i.is_a? AST::SimpleType && i.type == type.type }
        GenericType.new(type.type)
      end
    end
  end

  record TypedName, type : Type, name : String

  def resolve_type(type_name : AST::TypedName, fn_def : AST::FunctionDefinition? = nil, use_type_scope = true) : TypedName
    TypedName.new(resolve_type(type_name.type), type_name.name)
  end

  def resolve_type(type : AST::Type?, fn_def : AST::FunctionDefinition? = nil, use_type_scope = true) : Type
    return PrimitiveVoidType.new.as(Type) if type.nil?
    case type
    in AST::PtrType    then PointerType.new(resolve_type(type.type, fn_def, use_type_scope))
    in AST::SliceType  then SliceType.new(resolve_type(type.type, fn_def, use_type_scope))
    in AST::SimpleType then resolve_generic(type, fn_def, use_type_scope) || @types[type.type]
    in AST::SelfType   then @type_scope[""]? || @fn_self_types[fn_def || cur_ast_fn]
    in AST::Type       then raise "Invalid type #{type}"
    end
  end

  def llvm_type(type : Type) : LLVM::Type
    type.to_llvm(@ctx, @type_scope)
  end

  def llvm_value_type(type : Type) : LLVM::Type
    type.to_llvm_value_type(@ctx, @type_scope)
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
    ":#{m.try &.@pos.try &.begin}".rjust 5
  end

  def type_compatibility(a : Type, b : Type, gen : Hash(String, Type)) : Int32
    if a == b
      return 2
    else
      if b.is_a? GenericType
        if gen.has_key?(b.name) && gen[b.name] != a
          return 0
        else
          gen[b.name] = a
          return 1
        end
      elsif b.is_a? SliceType
        if a.is_a? SliceType
          return type_compatibility a.value, b.value, gen
        else
          return 0
        end
      elsif b.is_a? PointerType
        if a.is_a? PointerType
          return type_compatibility a.value, b.value, gen
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
    when AST::IntLiteral
      if ex.val < Int32::MAX && ex.val > Int32::MIN
        Value.new(@ctx.int32.const_int(ex.val.to_i32), Type::INT_32)
      else
        raise OverflowError.new
      end
    when AST::BoolLiteral
      Value.new(@ctx.int1.const_int(ex.val ? 1 : 0), Type::BOOL)
    when AST::CharLiteral
      chr = ex.val[0]
      if chr == '\\' # HACK
        if ex.val == "\\0"
          chr = '\0'
        else
          chr = ex.val[1]
        end
      end
      Value.new(@ctx.int8.const_int(chr.ord), Type::UINT_8)
    when AST::VariableLiteral
      var = @fn_scope[ex.name]
      Value.new(@builder.load(var.llvm), var.type)
    when AST::StringLiteral
      str = ex.val.gsub("\\n", "\n") # HACK
      str_val = @ctx.const_string str
      s = @mod.globals.add(str_val.type, "str:" + str.hash.to_s(16))
      s.global_constant = true
      s.linkage = LLVM::Linkage::Internal
      s.initializer = str_val
      Value.new(@builder.bit_cast(s, @ctx.int8.pointer), Type::UINT_8.ptr)
    when AST::ArrayLiteral
      args = ex.val.map { |i| expression i }
      val_type = resolve_type(ex.type)
      arr_val = llvm_type(val_type).const_array args.map(&.llvm)
      s = @mod.globals.add(llvm_type(val_type).array(args.size), "array")
      s.global_constant = true
      s.initializer = arr_val
      s.linkage = LLVM::Linkage::Internal
      array = @builder.bit_cast(s, llvm_type(PointerType.new(val_type)))
      slice = @ctx.const_struct([array, @ctx.int64.const_int(args.size)])
      Value.new(slice, SliceType.new(val_type))
    when AST::Call
      overload_set = @fns.keys.select(&.name.name.== ex.name.name)

      args = ex.args.map { |i| expression i }

      {% if flag?("debug_overload") %}
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
      {% end %}

      # TODO: better heuristics
      matching = overload_set.compact_map do |o|
        sig = type_signature(o)[0]
        next nil if sig.size > args.size
        valid = true
        compatibility = 0
        generic_match = {} of String => Type
        args.each_with_index do |a, i|
          if i >= sig.size
            break valid = (o.is_a?(AST::PrimitiveDefinition) && o.varargs?)
          end
          c = type_compatibility(a.type, sig[i], generic_match)
          compatibility += c
          break valid = false if c.zero?
        end
        valid ? {o, compatibility, generic_match} : nil
      end

      matching_fn_def, _, matching_generics = matching.max_by(&.[1])
      matching_fn = @fns[matching_fn_def]
      @type_scope = matching_generics

      {% if flag?("debug_overload") %}
        puts "#{def_pos matching_fn_def} > Selected (#{matching.size}) (#{matching_generics.map { |k, v| "#{k} => #{v}" }.join ", "})"
        puts
      {% end %}

      if matching_fn.nil? && matching_fn_def.is_a? AST::PrimitiveDefinition
        v = case matching_fn_def.primitive
            when "libc"    then @builder.call(@libc[ex.name.name], args.map(&.llvm))
            when "add"     then @builder.add(args[0], args[1])
            when "sub"     then @builder.sub(args[0], args[1])
            when "mul"     then @builder.mul(args[0], args[1])
            when "mod"     then signed_type?(args[0].type) ? @builder.srem(args[0], args[1]) : @builder.urem(args[0], args[1])
            when "int_div" then signed_type?(args[0].type) ? @builder.sdiv(args[0], args[1]) : @builder.udiv(args[0], args[1])
            when "icmp_eq" then @builder.icmp(LLVM::IntPredicate::EQ, args[0], args[1])
            when "icmp_ne" then @builder.icmp(LLVM::IntPredicate::NE, args[0], args[1])
            when "icmp_gt" then @builder.icmp((signed_type?(args[0].type) ? LLVM::IntPredicate::SGT : LLVM::IntPredicate::UGT), args[0], args[1])
            when "icmp_lt" then @builder.icmp((signed_type?(args[0].type) ? LLVM::IntPredicate::SLT : LLVM::IntPredicate::ULT), args[0], args[1])
            when "get_at"
              offset = @builder.inbounds_gep(args[0].llvm, args[1].llvm, "get_at.offset")
              @builder.load(offset, "get_at.load")
            when "set_at"
              offset = @builder.inbounds_gep args[0].llvm, args[1].llvm, "set_at.offset"
              val = args[2].llvm
              @builder.store val, offset
              args[2].llvm
            when "slice_size" then @builder.extract_value(args[0], 1, "slice.size")
            when "slice_ptr"  then @builder.extract_value(args[0], 0, "slice.ptr")
            when "slice_dup"
              slice_val = args[0].type.as(SliceType)
              val_type = slice_val.value
              old_arr_size = @builder.extract_value(args[0], 1, "d.extract")
              arr_size = @builder.add(old_arr_size, args[1])
              array = LLVM::Value.new LibLLVM.build_array_malloc(@builder, llvm_type(val_type), arr_size, "d.malloc")
              array_ptr = @builder.bit_cast(array, llvm_type(PointerType.new(val_type)))
              slice_alloc = @builder.alloca llvm_type(slice_val)
              slice_arr_ptr = @builder.inbounds_gep slice_alloc, @ctx.int32.const_int(0), @ctx.int32.const_int(0), "d.slice.arr.ptr"
              @builder.store array_ptr, slice_arr_ptr
              slice_size_ptr = @builder.inbounds_gep slice_alloc, @ctx.int32.const_int(0), @ctx.int32.const_int(1), "d.slice.size.ptr"
              @builder.store arr_size, slice_size_ptr
              @builder.load slice_alloc
            else raise "unknown primitive #{matching_fn_def.primitive}"
            end
        val = Value.new(v, resolve_type matching_fn_def.return_type)
        @type_scope.clear
        val
      else
        if matching_fn.nil?
          matching_fn = declare(matching_fn_def, always_instantiate: true).not_nil!
        end
        val = Value.new(@builder.call(matching_fn, args.map(&.llvm)), resolve_type(matching_fn_def.not_nil!.return_type))
        @type_scope.clear
        val
      end
    when AST::CtorCall
      struct_type = resolve_type ex.type
      if struct_type.is_a? StructType
        instance = LLVM::Value.new LibLLVM.build_malloc(@builder, llvm_value_type(struct_type), "ctor.malloc")
        ex.args.each_with_index do |field, i|
          field_ptr = @builder.inbounds_gep instance, @ctx.int32.const_int(0), @ctx.int32.const_int(i), "ctor.field.#{struct_type.fields[i].name}"
          @builder.store expression(field), field_ptr
        end
        Value.new instance, struct_type
      elsif struct_type.is_a? SliceType
        val_type = struct_type.value
        arr_size = expression ex.args[0]
        array = LLVM::Value.new LibLLVM.build_array_malloc(@builder, llvm_type(val_type), arr_size, "s.ctor.malloc")
        array_ptr = @builder.bit_cast(array, llvm_type(val_type).pointer, "s.ctor.malloc.ptr")
        slice_alloc = @builder.alloca llvm_type(struct_type)
        slice_arr_ptr = @builder.inbounds_gep slice_alloc, @ctx.int32.const_int(0), @ctx.int32.const_int(0), "d.slice.arr.ptr"
        @builder.store array_ptr, slice_arr_ptr
        slice_size_ptr = @builder.inbounds_gep slice_alloc, @ctx.int32.const_int(0), @ctx.int32.const_int(1), "d.slice.size.ptr"
        @builder.store arr_size, slice_size_ptr
        Value.new @builder.load(slice_alloc), struct_type
      elsif struct_type.is_a? IntegralType
        arg = expression ex.args[0]
        Value.new @builder.trunc(arg, llvm_type(struct_type), "i.ctor.#{struct_type}"), struct_type
      else
        raise "Cannot construct a non-struct, non-slice, non-integral type #{struct_type}"
      end
    when AST::FieldAccess
      object = expression ex.base
      object_type = object.type
      unless object_type.is_a? StructType
        raise "Cannot access the field of a non-struct type #{object_type}"
      end
      fields = object_type.fields
      matching_field = fields.find(&.name.== ex.field)
      raise "Unknown field #{ex.field} of object #{object_type}" if matching_field.nil?
      field_val = @builder.extract_value(@builder.load(object), fields.index!(matching_field), "field.#{ex.field}")
      Value.new field_val, matching_field.type
    when AST::Assignment
      case target = ex.target
      when AST::Call
        augmented_call = target.copy_with(
          args: target.args.dup,
          name: target.name.copy_with(name:
            target.name.name + "="
          )
        )
        augmented_call.args << ex.value
        expression(augmented_call)
      when AST::FieldAccess
        object = expression target.base
        value = expression ex.value
        object_type = object.type
        unless object_type.is_a? StructType
          raise "Cannot access the field of a non-struct type #{object_type}"
        end
        fields = object_type.fields
        matching_field = fields.find(&.name.== target.field)
        raise "Unknown field #{target.field} of object #{object_type}" if matching_field.nil?
        field_val = @builder.inbounds_gep(object.llvm, @ctx.int32.const_int(0), @ctx.int32.const_int(fields.index!(matching_field)), "field.#{target.field}")
        stored_val = @builder.store(value, field_val)
        value
      when AST::VariableLiteral
        raise "Not declared #{ex}" unless @fn_scope.has_key?(target.name)
        value = expression(ex.value)
        stored_val = @builder.store(value, @fn_scope[target.name].llvm)
        value
      else
        p! target
        raise "Not implemented"
      end
    else
      # STDERR.puts "Unknown expression #{ex}"
      # Value.new(llvm_type(resolve_type(cur_ast_fn.return_type)).null, resolve_type cur_ast_fn.return_type)
      raise "Unknown expression #{ex}"
    end
  end

  def statement(st : AST::Statement)
    case st
    when AST::ReturnStatement
      if expr = st.expression
        @builder.ret(expression(expr).llvm)
      else
        @builder.ret
      end
      @terminated = true
    when AST::DeclarationStatement
      raise "Duplicate declaration #{st}" if @fn_scope.has_key?(st.name)

      # TODO: Why does the type scope need to be saved here? Expressions probably shouldn't
      # clobber the type scope like this, need to investigate if it can cause issues down the road
      saved_type_scope = @type_scope
      value = expression(st.value)
      @type_scope = saved_type_scope
      type = st.type.nil? ? value.type : resolve_type st.type

      lv = hoisted { @builder.alloca(llvm_type(type), st.name) }

      @fn_scope[st.name] = Value.new(lv, type)
      @builder.store(value, lv)
    when AST::ExpressionStatement
      expression(st.expression)
    when AST::IfStatement
      merge_bb = cur_llvm_fn.basic_blocks.append("if.cont")
      next_test_bb = cur_llvm_fn.basic_blocks.append("if.test")
      else_bb : LLVM::BasicBlock? = nil
      @builder.br next_test_bb
      body_terminated = true
      st.clauses.each_with_index do |clause, i|
        test_bb = next_test_bb
        body_bb = cur_llvm_fn.basic_blocks.append("if.then")
        if i + 1 >= st.clauses.size
          if else_bb.nil? && st.else_clause
            else_bb = cur_llvm_fn.basic_blocks.append("if.else")
          end
          next_test_bb = else_bb || merge_bb
        else
          next_test_bb = cur_llvm_fn.basic_blocks.append("if.test")
        end
        @builder.position_at_end test_bb
        condition = expression(clause.condition)
        @builder.cond condition, body_bb, next_test_bb
        @builder.position_at_end body_bb
        compile_body(clause.body)
        unless @terminated
          body_terminated = @terminated
          @builder.br merge_bb
        end
      end
      @terminated = false
      if else_clause = st.else_clause
        @builder.position_at_end else_bb.not_nil!
        compile_body(else_clause.statements)
        @builder.br merge_bb unless @terminated
      end
      if @terminated && body_terminated
        merge_bb.delete
      else
        @terminated = false
        @builder.position_at_end merge_bb
      end
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
      raise "Unknown statement #{st}"
    end
  end

  def type_signature(fn : AST::FunctionDefinition) : {Slice(Type), Type}
    args = Slice(Type).new(fn.args.size) do |i|
      resolve_type(fn.args[i].type, fn, false)
    end
    {args, resolve_type(fn.return_type, fn, false)}
  end

  def hoisted(&)
    entry = cur_llvm_fn.basic_blocks.first
    current = @builder.insert_block
    @builder.position_at_end entry
    r = yield
    @builder.position_at_end current
    r
  end

  def compile_body(sts : Array(AST::Statement))
    @terminated = false
    sts.each do |st|
      statement st
    end
  end

  def signed_type?(t : Type) : Bool
    raise "Can't check signedness of non-integral type" unless t.is_a? IntegralType
    t.signed
  end

  def declare(fn : AST::FunctionDefinition, always_instantiate : Bool = false) : LLVM::Function?
    # TODO: rehaul all of this logic, it is quite ugly
    # Functions should be instantiated only when used, declaration should basically do nothing
    # TODO: What is the difference between AST::FunctionDefinition and LLVM::Function, how are they used
    # and why are they separate?
    # TODO: Handle namespaced stuff properly
    # TODO: proper reference semantics
    if fn.decl.generics && !always_instantiate
      @fns[fn] = nil
    else
      arg_types = fn.args.map { |i| llvm_type(resolve_type(i.type, fn)) }
      ret_type = llvm_type(resolve_type(fn.return_type, fn))
      if fn.is_a? AST::PrimitiveDefinition
        f = nil
      else
        llvm_name = "ym:#{fn.name.name}(#{fn.args.map { |i| resolve_type(i.type).to_s }.join ","})"
        f = @mod.functions.add llvm_name, arg_types, ret_type
      end
      unless fn.decl.generics
        @fns[fn] = f
      end
      if f
        @instantiation_queue << {fn, f, @type_scope.dup}
      end
      f
    end
  end

  def add_libc_functions
    @libc["puts"] = @mod.functions.add("puts", [@ctx.int8.pointer], @ctx.int32)
    @libc["printf"] = @mod.functions.add("printf", [@ctx.int8.pointer], @ctx.int32, varargs: true)
    @libc["donothing"] = @mod.functions.add("llvm.donothing", [] of LLVM::Type, @ctx.void)
    @libc.each do |k, v|
      v.linkage = LLVM::Linkage::External
    end
  end
end
