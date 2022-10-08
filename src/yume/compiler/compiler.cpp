#include "compiler.hpp"
#include "ast/ast.hpp"
#include "compiler/type_holder.hpp"
#include "diagnostic/errors.hpp"
#include "extra/mangle.hpp"
#include "qualifier.hpp"
#include "semantic/type_walker.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include "vals.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringMapEntry.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/ADT/iterator.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetOptions.h>
// TODO(LLVM MIN >= 14): remove workaround
#if __has_include(<llvm/MC/TargetRegistry.h>)
#include <llvm/MC/TargetRegistry.h>
#else
#include <llvm/Support/TargetRegistry.h>
#endif
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace yume {
static auto make_cdtor_fn(llvm::IRBuilder<>& builder, llvm::Module& module, bool is_ctor) -> llvm::Function* {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static auto* global_cdtor_fn_ty = llvm::FunctionType::get(builder.getVoidTy(), false);
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static auto* global_cdtor_entry_ty =
      llvm::StructType::get(builder.getInt32Ty(), global_cdtor_fn_ty->getPointerTo(), builder.getInt8PtrTy());
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static auto* global_cdtor_array_ty = llvm::ArrayType::get(global_cdtor_entry_ty, 1);

  auto* global_cdtor_fn = llvm::Function::Create(global_cdtor_fn_ty, llvm::Function::ExternalLinkage,
                                                 (is_ctor ? "_Ym.__ctor" : "_Ym.__dtor"), &module);
  auto* bb = llvm::BasicBlock::Create(module.getContext(), "entry", global_cdtor_fn);
  builder.SetInsertPoint(bb);
  builder.CreateRetVoid();

  (void)new llvm::GlobalVariable(
      module, global_cdtor_array_ty, true, llvm::GlobalVariable::AppendingLinkage,
      llvm::ConstantArray::get(
          global_cdtor_array_ty,
          llvm::ConstantStruct::get(global_cdtor_entry_ty, builder.getInt32(std::numeric_limits<uint16_t>::max()),
                                    global_cdtor_fn, llvm::ConstantPointerNull::get(builder.getInt8PtrTy()))),
      (is_ctor ? "llvm.global_ctors" : "llvm.global_dtors"));

  return global_cdtor_fn;
}

Compiler::Compiler(const optional<string>& target_triple, vector<SourceFile> source_files)
    : m_sources(move(source_files)), m_walker(std::make_unique<semantic::TypeWalker>(*this)) {
  m_context = std::make_unique<llvm::LLVMContext>();
  m_module = std::make_unique<llvm::Module>("yume", *m_context);
  m_builder = std::make_unique<llvm::IRBuilder<>>(*m_context);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();
  string error;
  const string triple = target_triple.value_or(llvm::sys::getDefaultTargetTriple());
  const auto* target = llvm::TargetRegistry::lookupTarget(triple, error);

  if (target == nullptr) {
    errs() << error;
    throw std::exception();
  }
  const char* cpu = "generic";
  const char* feat = "";

  const llvm::TargetOptions opt;
  m_targetMachine =
      unique_ptr<llvm::TargetMachine>(target->createTargetMachine(triple, cpu, feat, opt, llvm::Reloc::Model::PIC_));

  m_module->setDataLayout(m_targetMachine->createDataLayout());
  m_module->setTargetTriple(triple);

  m_types.declare_size_type(*this);

  m_global_ctor_fn = make_cdtor_fn(*m_builder, *m_module, true);
  m_global_dtor_fn = make_cdtor_fn(*m_builder, *m_module, false);
}

void Compiler::declare_default_ctor(Struct& st) {
  const bool no_ctors_declared =
      std::ranges::none_of(m_ctors, [&](const Fn& fn) { return fn.get_self_ty() == st.get_self_ty(); });

  if (!no_ctors_declared)
    return; // Don't declare implicit ctors if at least one user-defined one exists

  vector<ast::TypeName> ctor_args;
  vector<ast::AnyStmt> ctor_body;
  for (auto& field : st.ast().fields) {
    auto tok = field.token_range();
    ast::AnyType proxy_type = make_unique<ast::ProxyType>(tok, field.name);
    ctor_args.emplace_back(field.token_range(), move(proxy_type), field.name);

    auto implicit_field = make_unique<ast::FieldAccessExpr>(tok, std::nullopt, field.name);
    auto arg_var = make_unique<ast::VarExpr>(tok, field.name);
    ctor_body.emplace_back(make_unique<ast::AssignExpr>(tok, move(implicit_field), move(arg_var)));
  }
  // TODO(rymiel): Give these things sensible locations?
  auto& new_ct = st.body().body.emplace_back(
      std::make_unique<ast::CtorDecl>(span<Token>{}, move(ctor_args), ast::Compound({}, move(ctor_body))));

  walk_types(decl_statement(*new_ct, st.get_self_ty(), st.member));
}

void Compiler::run() {
  m_scope.push_scope(); // Global scope

  for (const auto& source : m_sources)
    for (auto& i : source.program->body)
      decl_statement(*i, {}, source.program.get());

  // 1: Only convert the types of constants
  for (auto& cn : m_consts)
    walk_types(&cn);

  // 2: Only convert structs
  for (auto& st : m_structs)
    walk_types(&st);

  // 3: Convert initializers of constants
  for (auto& cn : m_consts) {
    auto* const_ty = llvm_type(cn.ast().ensure_ty());
    cn.llvm = new llvm::GlobalVariable(*m_module, const_ty, false, llvm::GlobalVariable::PrivateLinkage, nullptr,
                                       ".const." + cn.name());
  }

  // 4: Only convert user defined constructors
  for (auto& ct : m_ctors)
    walk_types(&ct);

  // At this point, all _user defined_ constructors have been declared, so we can add implicitly defined constructors to
  // structs which haven't declared any.
  for (auto& st : m_structs)
    declare_default_ctor(st);

  // 5: only convert function parameters
  for (auto& fn : m_fns)
    walk_types(&fn);

  // 6: convert everything else, but only when instantiated
  m_walker->in_depth = true;

  for (auto& cn : m_consts) {
    walk_types(&cn);
    define(cn);
  }

  // Find all external functions. These will be the "entrypoints".
  vector<Fn*> extern_fns = {};
  for (auto& fn : m_fns) {
    if (fn.name() == "main")
      fn.make_extern_linkage();

    if (fn.extern_linkage() && !fn.extern_decl())
      extern_fns.push_back(&fn);
  }

  if (extern_fns.empty())
    throw std::logic_error("Program is missing any declarations with external linkage. Perhaps you meant to declare a "
                           "`main` function?"); // Related: #10
  for (auto* ext : extern_fns)
    declare(*ext);

  while (!m_decl_queue.empty()) {
    auto next = m_decl_queue.front();
    m_decl_queue.pop();
    next.visit([](std::monostate /*absent*/) { /* nothing to do */ }, //
               [&](Fn* fn) { define(*fn); },                          //
               [&](Const* cn) { define(*cn); },
               [&](Struct* /*st*/) { throw std::logic_error("Cannot define a struct"); });
  }

  yume_assert(m_scope.size() == 1, "End of compilation should end with only the global scope remaining");
  m_builder->SetInsertPoint(&m_global_dtor_fn->getEntryBlock(), m_global_dtor_fn->getEntryBlock().begin());
  destruct_last_scope();
  m_scope.clear();
}

void Compiler::walk_types(DeclLike decl_like) {
  decl_like.visit([](std::monostate /*absent*/) { /* nothing to do */ },
                  [&](auto& decl) {
                    m_walker->current_decl = decl;
                    m_walker->body_statement(decl->ast());
                    m_walker->current_decl = {};
                  });
}

auto Compiler::create_struct(Struct& st) -> bool {
  auto& s_decl = st.st_ast;

  auto fields = vector<ast::TypeName*>();
  fields.reserve(s_decl.fields.size());
  for (auto& f : s_decl.fields)
    fields.push_back(&f);

  auto iter = m_types.known.find(s_decl.name);
  if (iter == m_types.known.end()) {
    auto empl =
        m_types.known.try_emplace(s_decl.name, std::make_unique<ty::Struct>(s_decl.name, move(fields), &st.subs));
    yume_assert(isa<ty::Struct>(*empl.first->second));
    st.self_ty = &*empl.first->second;
    return true;
  }

  yume_assert(isa<ty::Struct>(*iter->second));
  auto& existing = cast<ty::Struct>(*iter->second);

  if (std::ranges::any_of(st.subs, [](const auto& sub) noexcept { return sub.second.is_generic(); }))
    return false;

  existing.m_fields = move(fields);
  st.self_ty = &existing.emplace_subbed(st.subs);
  return true;
}

auto Compiler::decl_statement(ast::Stmt& stmt, optional<ty::Type> parent, ast::Program* member) -> DeclLike {
  if (auto* fn_decl = dyn_cast<ast::FnDecl>(&stmt)) {
    vector<unique_ptr<ty::Generic>> type_args{};
    Substitution subs{};
    for (auto& i : fn_decl->type_args) {
      auto& gen = type_args.emplace_back(std::make_unique<ty::Generic>(i));
      subs.try_emplace(i, gen.get());
    }
    auto& fn = m_fns.emplace_back(fn_decl, member, parent, move(subs), move(type_args));

    return &fn;
  }
  if (auto* s_decl = dyn_cast<ast::StructDecl>(&stmt)) {
    vector<unique_ptr<ty::Generic>> type_args{};
    Substitution subs{};
    for (auto& i : s_decl->type_args) {
      auto& gen = type_args.emplace_back(std::make_unique<ty::Generic>(i));
      subs.try_emplace(i, gen.get());
    }
    auto& st = m_structs.emplace_back(*s_decl, member, std::nullopt, subs, move(type_args));
    if (!create_struct(st)) {
      m_structs.pop_back();
      return {};
    }

    if (st.name() == "Slice") // TODO(rymiel): magic value?
      m_slice_struct = &st;

    for (auto& f : s_decl->body)
      if (st.type_args.empty() || isa<ast::CtorDecl>(*f))
        decl_statement(*f, st.self_ty, member);

    return &st;
  }
  if (auto* ctor_decl = dyn_cast<ast::CtorDecl>(&stmt)) {
    auto& ctor = m_ctors.emplace_back(ctor_decl, member, parent);

    return &ctor;
  }
  if (auto* const_decl = dyn_cast<ast::ConstDecl>(&stmt)) {
    auto& cn = m_consts.emplace_back(*const_decl, member, parent);

    return &cn;
  }

  throw std::runtime_error("Invalid top-level statement: "s + stmt.kind_name());
}

static auto build_function_type(Compiler& compiler, const ty::Function& type) -> llvm::Type* {
  llvm::StructType* closure_ty = nullptr;
  llvm::Type* memo = nullptr;
  auto* erased_closure_ty = compiler.builder()->getInt8PtrTy();

  if (!type.is_fn_ptr()) {
    auto closured = vector<llvm::Type*>{};
    for (const auto& i : type.closure())
      closured.push_back(compiler.llvm_type(i));
    if (closured.empty()) // Can't have an empty closure type
      closured.push_back(compiler.builder()->getInt8Ty());

    // Closures are type-erased to just a bag of bits. We store the "real type" of the closure within the function
    // type for better retrieval, but in practice, they're always passed around bitcasted. Note that the bitcasting
    // is not required when opaque pointers are in play
    closure_ty = llvm::StructType::create(closured, "closure");
  }

  auto args = vector<llvm::Type*>{};
  if (closure_ty != nullptr)
    args.push_back(erased_closure_ty); // Lambda takes a closure as the first parameter
  for (const auto& i : type.args())
    args.push_back(compiler.llvm_type(i));

  auto* return_type = compiler.builder()->getVoidTy();
  if (auto ret = type.ret(); ret.has_value())
    return_type = compiler.llvm_type(*ret);

  auto* fn_ty = llvm::FunctionType::get(return_type, args, type.is_c_varargs());

  if (type.is_fn_ptr()) {
    memo = fn_ty->getPointerTo();
  } else {
    memo = llvm::StructType::get(fn_ty->getPointerTo(), erased_closure_ty);
  }

  type.fn_memo(compiler, fn_ty);
  type.closure_memo(compiler, closure_ty);
  type.memo(compiler, memo);
  return memo;
}

auto Compiler::llvm_type(ty::Type type) -> llvm::Type* {
  llvm::Type* base = nullptr;

  if (const auto* int_type = type.base_dyn_cast<ty::Int>()) {
    base = llvm::Type::getIntNTy(*m_context, int_type->size());
  } else if (const auto* ptr_type = type.base_dyn_cast<ty::Ptr>()) {
    yume_assert(ptr_type->qualifier() == Qualifier::Ptr, "Ptr type must hold pointer");
    base = llvm::PointerType::getUnqual(llvm_type(ptr_type->pointee()));
  } else if (const auto* struct_type = type.base_dyn_cast<ty::Struct>()) {
    auto* memo = struct_type->memo();
    if (memo == nullptr) {
      auto fields = vector<llvm::Type*>{};
      for (const auto* i : struct_type->fields())
        fields.push_back(llvm_type(i->type->ensure_ty()));

      memo = llvm::StructType::create(*m_context, fields, "_"s + struct_type->name());
      struct_type->memo(memo);
    }

    base = memo;
  } else if (const auto* function_type = type.base_dyn_cast<ty::Function>()) {
    llvm::Type* memo = function_type->memo();
    if (memo == nullptr)
      memo = build_function_type(*this, *function_type);

    base = memo;
  } else if (type.base_isa<ty::Nil>()) {
    base = llvm::Type::getVoidTy(*m_context);
  } else {
    throw std::logic_error("Unknown type base " + type.name());
  }

  if (type.is_mut())
    return base->getPointerTo();
  return base;
}

void Compiler::destruct(Val val, ty::Type type) {
  if (type.is_mut()) {
    const auto deref_type = type.mut_base();
    if (deref_type.has_value())
      return destruct(m_builder->CreateLoad(llvm_type(*deref_type), val, "dt.deref"), *deref_type);
  }
  if (type.is_slice()) {
    auto* ptr = m_builder->CreateExtractValue(val, 0, "sl.ptr.free");
    auto* free = llvm::CallInst::CreateFree(ptr, m_builder->GetInsertBlock());
    m_builder->Insert(free);
  }
}

auto Compiler::default_init(ty::Type type) -> Val {
  if (type.is_mut())
    throw std::runtime_error("Cannot default-initialize a reference");
  if (const auto* int_type = type.base_dyn_cast<ty::Int>())
    return m_builder->getIntN(int_type->size(), 0);
  if (const auto* ptr_type = type.base_dyn_cast<ty::Ptr>()) {
    switch (ptr_type->qualifier()) {
    default: llvm_unreachable("Ptr type cannot hold this qualifier");
    case Qualifier::Ptr:
      llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(llvm_type(ptr_type->pointee())));
      break;
    }
  }
  if (const auto* struct_type = type.base_dyn_cast<ty::Struct>()) {
    auto* llvm_ty = cast<llvm::StructType>(llvm_type(type));
    Val val = llvm::UndefValue::get(llvm_ty);

    for (const auto& i : llvm::enumerate(struct_type->fields()))
      val = m_builder->CreateInsertValue(val, default_init(i.value()->type->ensure_ty()), i.index());

    return val;
  }

  throw std::runtime_error("Cannot default-initialize "s + type.name());
}

auto Compiler::declare(Fn& fn) -> llvm::Function* {
  if (fn.llvm != nullptr)
    return fn.llvm;
  if (fn.primitive())
    return nullptr;

  yume_assert(fn.fn_ty != nullptr, "Function declaration "s + fn.name() + " has no function type set");
  (void)llvm_type(fn.fn_ty); // Ensure we've created a function type
  llvm::FunctionType* fn_t = fn.fn_ty->fn_memo();

  string name = fn.name();
  if (!fn.extern_linkage() && !fn.local())
    name = mangle::mangle_name(fn);

  auto linkage = fn.extern_linkage() ? llvm::Function::ExternalLinkage
                 : fn.local()        ? llvm::Function::PrivateLinkage
                                     : llvm::Function::InternalLinkage;
  auto* llvm_fn = llvm::Function::Create(fn_t, linkage, name, m_module.get());
  if (fn.has_annotation("interrupt") && fn.fn_ty->closure_memo() == nullptr) // HACK
    llvm_fn->setCallingConv(llvm::CallingConv::X86_INTR);
  fn.llvm = llvm_fn;

  auto arg_names = fn.arg_names();
  if (fn.fn_ty->closure_memo() != nullptr)
    arg_names.insert(arg_names.begin(), "<closure>"); // For functions with closures, it is the first argument
  for (auto [llvm_arg, arg_name] : llvm::zip(llvm_fn->args(), arg_names))
    llvm_arg.setName("arg."s + arg_name);

  // At this point, the function prototype is declared, but not the body.
  // In the case of extern or local functions, a prototype is all that will be declared.
  if (!fn.extern_decl() && !fn.local()) {
    m_decl_queue.emplace(&fn);
    m_walker->current_decl = &fn;
    m_walker->body_statement(fn.ast());
  }
  return llvm_fn;
}

template <> void Compiler::statement(ast::Compound& stat) {
  auto guard = m_scope.push_scope_guarded();
  for (auto& i : stat)
    body_statement(*i);

  if (m_builder->GetInsertBlock()->getTerminator() == nullptr)
    destruct_last_scope();
}

static void destruct_indirect(Compiler& compiler, const InScope& v) {
  const auto ty = v.ast.ensure_ty();
  if (v.owning && !ty.is_trivially_destructible()) {
    yume_assert(v.value.llvm->getType() == compiler.llvm_type(ty.without_mut())->getPointerTo());
    Val llvm_val = v.value;
    if (!ty.is_mut())
      llvm_val = compiler.builder()->CreateLoad(compiler.llvm_type(ty), v.value, "dt.l");
    compiler.destruct(llvm_val, ty);
  }
}

void Compiler::destruct_last_scope() {
  for (const auto& i : m_scope.last_scope()) {
    destruct_indirect(*this, i.second);
  }
}

void Compiler::destruct_all_scopes() {
  for (const auto& scope : llvm::reverse(llvm::drop_begin(m_scope.all_scopes()))) {
    for (const auto& i : scope) {
      destruct_indirect(*this, i.second);
    }
  }
}

void Compiler::expose_parameter_as_local(ty::Type type, const string& name, const ast::AST& ast, Val val) {
  if (type.is_mut()) {
    m_scope.add(name, {.value = val, .ast = ast, .owning = false}); // We don't own parameters
    return;
  }
  Val alloc = m_builder->CreateAlloca(llvm_type(type), nullptr, "lv."s + name);
  m_builder->CreateStore(val, alloc);
  m_scope.add(name, {.value = alloc, .ast = ast, .owning = false}); // We don't own parameters
}

void Compiler::setup_fn_base(Fn& fn) {
  m_current_fn = &fn;
  m_scope.push_scope();
  m_scope_ctor.reset();
  auto* bb = llvm::BasicBlock::Create(*m_context, "entry", fn.llvm);
  m_builder->SetInsertPoint(bb);

  // If this function has a closure, it is the first parameter and will be skipped below
  auto arg_offset = fn.fn_ty->closure_memo() == nullptr ? 0 : 1;
  auto llvm_fn_args = llvm::drop_begin(fn.llvm->args(), arg_offset);
  // Allocate local variables for each parameter
  for (auto [arg, ast_arg] : llvm::zip(llvm_fn_args, fn.args())) {
    expose_parameter_as_local(ast_arg.type, ast_arg.name, ast_arg.ast, &arg);
  }
}

void Compiler::define(Const& cn) {
  if (cn.llvm->hasInitializer())
    return;

  auto* saved_insert_block = m_builder->GetInsertBlock();
  m_builder->SetInsertPoint(&m_global_ctor_fn->getEntryBlock(), m_global_ctor_fn->getEntryBlock().begin());

  auto init = body_expression(*cn.ast().init);
  if (isa<llvm::Constant>(init.llvm)) {
    cn.llvm->setConstant(true);
    auto* const_val = cast<llvm::Constant>(init.llvm);
    cn.llvm->setInitializer(const_val);
  } else {
    cn.llvm->setInitializer(llvm::ConstantAggregateZero::get(cn.llvm->getValueType()));
    m_builder->CreateStore(init.llvm, cn.llvm);
  }

  m_builder->SetInsertPoint(saved_insert_block);
}

void Compiler::define(Fn& fn) {
  setup_fn_base(fn);

  if (isa<ast::FnDecl>(&fn.ast())) {
    if (auto* body = get_if<ast::Compound>(&fn.fn_body()); body != nullptr) {
      statement(*body);
    }

    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      destruct_all_scopes();
      m_builder->CreateRetVoid();
    }
  } else {
    yume_assert(fn.self_ty.has_value(), "Cannot define constructor when the type being constructed is unknown");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): clang-tidy doesn't accept yume_assert as an assertion
    auto* ctor_type = llvm_type(*fn.self_ty);
    const Val base_value = llvm::UndefValue::get(ctor_type);
    auto* base_alloc = m_builder->CreateAlloca(ctor_type, nullptr, "ctor.base");
    m_builder->CreateStore(base_value, base_alloc);

    // Act as if we don't own the object being constructed so it won't get destructed at the end of scope
    m_scope_ctor.emplace(InScope{.value = base_alloc, .ast = fn.ast(), .owning = false});

    statement(fn.compound_body());

    destruct_all_scopes();
    auto* finalized_value = m_builder->CreateLoad(ctor_type, base_alloc);
    m_builder->CreateRet(finalized_value);
  }

  m_scope.pop_scope();
  yume_assert(m_scope.size() == 1, "End of function should end with only the global scope remaining");
  verifyFunction(*fn.llvm, &errs());
}

template <> void Compiler::statement(ast::WhileStmt& stat) {
  auto* test_bb = llvm::BasicBlock::Create(*m_context, "while.test", m_current_fn->llvm);
  auto* head_bb = llvm::BasicBlock::Create(*m_context, "while.head", m_current_fn->llvm);
  auto* merge_bb = llvm::BasicBlock::Create(*m_context, "while.merge", m_current_fn->llvm);
  m_builder->CreateBr(test_bb);
  m_builder->SetInsertPoint(test_bb);
  auto cond_value = body_expression(*stat.cond);
  m_builder->CreateCondBr(cond_value, head_bb, merge_bb);
  m_builder->SetInsertPoint(head_bb);
  body_statement(stat.body);
  m_builder->CreateBr(test_bb);
  m_builder->SetInsertPoint(merge_bb);
}

template <> void Compiler::statement(ast::IfStmt& stat) {
  auto* merge_bb = llvm::BasicBlock::Create(*m_context, "if.cont", m_current_fn->llvm);
  auto* next_test_bb = llvm::BasicBlock::Create(*m_context, "if.test", m_current_fn->llvm, merge_bb);
  auto* last_branch = m_builder->CreateBr(next_test_bb);
  bool all_terminated = true;

  auto& clauses = stat.clauses;
  for (auto& clause : clauses) {
    m_builder->SetInsertPoint(next_test_bb);
    auto* body_bb = llvm::BasicBlock::Create(*m_context, "if.then", m_current_fn->llvm, merge_bb);
    next_test_bb = llvm::BasicBlock::Create(*m_context, "if.test", m_current_fn->llvm, merge_bb);
    auto condition = body_expression(*clause.cond);
    last_branch = m_builder->CreateCondBr(condition, body_bb, next_test_bb);
    m_builder->SetInsertPoint(body_bb);
    statement(clause.body);
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      all_terminated = false;
      m_builder->CreateBr(merge_bb);
    }
  }

  auto& else_clause = stat.else_clause;
  if (else_clause.has_value()) {
    next_test_bb->setName("if.else");
    m_builder->SetInsertPoint(next_test_bb);
    statement(*else_clause);
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      all_terminated = false;
      m_builder->CreateBr(merge_bb);
    }
  } else {
    last_branch->setSuccessor(1, merge_bb);
    next_test_bb->eraseFromParent();
  }

  if (all_terminated)
    merge_bb->eraseFromParent();
  else
    m_builder->SetInsertPoint(merge_bb);
}

template <> void Compiler::statement(ast::ReturnStmt& stat) {
  InScope* reset_owning = nullptr;

  if (stat.expr.has_value()) {
    // Returning a local variable also gives up ownership of it
    if (stat.extends_lifetime != nullptr) {
      for (auto& i : m_scope.last_scope()) {
        auto& v = i.second;
        if (&v.ast == stat.extends_lifetime) {
          v.owning = false;
          reset_owning = &v;
          break;
        }
      }
    }

    destruct_all_scopes();

    if (reset_owning != nullptr)
      reset_owning->owning = true; // The local variable may not be returned in all code paths, so reset its ownership

    auto val = body_expression(*stat.expr);
    if (val.llvm->getType()->isVoidTy())
      m_builder->CreateRetVoid();
    else
      m_builder->CreateRet(val);

    return;
  }

  destruct_all_scopes();
  m_builder->CreateRetVoid();
}

auto Compiler::entrypoint_builder() -> llvm::IRBuilder<> {
  if (m_current_fn == nullptr)
    return {&m_global_ctor_fn->getEntryBlock(), m_global_ctor_fn->getEntryBlock().begin()};
  return {&m_current_fn->llvm->getEntryBlock(), m_current_fn->llvm->getEntryBlock().begin()};
}

template <> void Compiler::statement(ast::VarDecl& stat) {
  // Locals are currently always mut, get the base type instead
  // TODO(rymiel): revisit, probably extract logic
  auto* var_type = llvm_type(stat.ensure_ty().ensure_mut_base());

  if (stat.init->ensure_ty().is_mut()) {
    auto expr_val = body_expression(*stat.init);
    m_scope.add(stat.name, {.value = expr_val, .ast = stat, .owning = false});
    return;
  }

  auto* alloc = entrypoint_builder().CreateAlloca(var_type, nullptr, "vdecl."s + stat.name);

  auto expr_val = body_expression(*stat.init);

  if (expr_val.scope != nullptr && expr_val.scope->owning)
    expr_val.scope->owning = false;

  m_builder->CreateStore(expr_val, alloc);
  m_scope.add(stat.name, {.value = alloc, .ast = stat, .owning = true});
}

template <> auto Compiler::expression(ast::NumberExpr& expr) -> Val {
  auto val = expr.val;
  if (expr.ensure_ty().base() == m_types.int64().s_ty)
    return m_builder->getInt64(val);
  return m_builder->getInt32(val);
}

template <> auto Compiler::expression(ast::CharExpr& expr) -> Val { return m_builder->getInt8(expr.val); }

template <> auto Compiler::expression(ast::BoolExpr& expr) -> Val { return m_builder->getInt1(expr.val); }

void Compiler::make_temporary_in_scope(Val& val, const ast::AST& ast, const string& name) {
  const string tmp_name = name + " " + ast.location().to_string();
  auto* md_node =
      llvm::MDNode::get(*m_context, llvm::MDString::get(*m_context, std::to_string(m_scope.size()) + ": " + tmp_name));

  auto ast_ty = ast.ensure_ty();
  yume_assert(llvm_type(ast_ty) == val.llvm->getType());
  auto* val_ty = llvm_type(ast_ty.without_mut());
  Val ptr = nullptr;
  if (m_scope.size() > 1) {
    auto* alloc = entrypoint_builder().CreateAlloca(val_ty, nullptr, name);
    alloc->setMetadata("yume.tmp", md_node);
    ptr = alloc;
  } else {
    auto* global = new llvm::GlobalVariable(*m_module, val_ty, false, llvm::GlobalVariable::PrivateLinkage,
                                            llvm::ConstantAggregateZero::get(val_ty), name);
    global->setMetadata("yume.tmp", md_node);
    ptr = global;
  }

  auto [iter, ok] = m_scope.add(tmp_name, {.value = ptr.llvm, .ast = ast, .owning = true});
  m_builder->CreateStore(ast_ty.is_mut() ? m_builder->CreateLoad(val_ty, val.llvm) : val.llvm, ptr.llvm);
  val.scope = &iter->second;
  val.scope->value = ptr;
}

template <> auto Compiler::expression(ast::StringExpr& expr) -> Val {
  auto val = expr.val;

  vector<llvm::Constant*> chars(val.length());
  for (unsigned int i = 0; i < val.size(); i++)
    chars[i] = m_builder->getInt8(val[i]);

  auto* base_type = m_builder->getInt8Ty();
  auto* global_string_type = llvm::ArrayType::get(base_type, chars.size());
  auto* global_init = llvm::ConstantArray::get(global_string_type, chars);
  auto* global = new llvm::GlobalVariable(*m_module, global_string_type, true, llvm::GlobalVariable::PrivateLinkage,
                                          global_init, ".str");

  auto* global_string_ptr = llvm::ConstantExpr::getBitCast(global, m_builder->getInt8PtrTy(0));

  auto* slice_size = m_builder->getIntN(ptr_bitsize(), val.length());
  auto* slice_type = cast<llvm::StructType>(llvm_type(expr.ensure_ty()));

  const Val string_alloc = create_malloc(base_type, slice_size, "str.ctor.malloc");
  m_builder->CreateMemCpy(string_alloc, {}, global_string_ptr, {}, slice_size);

  Val string_slice = llvm::UndefValue::get(slice_type);
  string_slice = m_builder->CreateInsertValue(string_slice, string_alloc, 0);
  string_slice = m_builder->CreateInsertValue(string_slice, slice_size, 1);

  make_temporary_in_scope(string_slice, expr, "tmps");

  return string_slice;
}

template <> auto Compiler::expression(ast::VarExpr& expr) -> Val {
  auto* in_scope = m_scope.find(expr.name);
  yume_assert(in_scope != nullptr, "Variable "s + expr.name + " is not in scope");
  auto* val = in_scope->value.llvm;
  // Function arguments act as locals, but they are immutable, but still behind a reference (alloca)
  if (!in_scope->ast.ensure_ty().is_mut())
    return m_builder->CreateLoad(llvm_type(in_scope->ast.ensure_ty()), val);

  return val;
}

template <> auto Compiler::expression(ast::ConstExpr& expr) -> Val {
  for (const auto& cn : m_consts) {
    if (cn.referred_to_by(expr))
      return m_builder->CreateLoad(llvm_type(cn.ast().ensure_ty()), cn.llvm, "cn." + expr.name);
  }

  throw std::runtime_error("Nonexistent constant called "s + expr.name);
}

/// A constexpr-friendly simple string hash, for simple switches with string cases
static auto constexpr const_hash(const char* input) -> unsigned {
  return *input != 0 ? static_cast<unsigned int>(*input) + 33 * const_hash(input + 1) : 5381; // NOLINT
}

auto Compiler::int_bin_primitive(const string& primitive, const vector<Val>& args) -> Val {
  const auto& a = args.at(0);
  const auto& b = args.at(1);
  auto hash = const_hash(primitive.data());
  switch (hash) {
  case const_hash("ib_icmp_sgt"): return m_builder->CreateICmpSGT(a, b);
  case const_hash("ib_icmp_ugt"): return m_builder->CreateICmpUGT(a, b);
  case const_hash("ib_icmp_slt"): return m_builder->CreateICmpSLT(a, b);
  case const_hash("ib_icmp_ult"): return m_builder->CreateICmpULT(a, b);
  case const_hash("ib_icmp_eq"): return m_builder->CreateICmpEQ(a, b);
  case const_hash("ib_icmp_ne"): return m_builder->CreateICmpNE(a, b);
  case const_hash("ib_add"): return m_builder->CreateAdd(a, b);
  case const_hash("ib_sub"): return m_builder->CreateSub(a, b);
  case const_hash("ib_mul"): return m_builder->CreateMul(a, b);
  case const_hash("ib_and"): return m_builder->CreateAnd(a, b);
  case const_hash("ib_srem"): return m_builder->CreateSRem(a, b);
  case const_hash("ib_urem"): return m_builder->CreateURem(a, b);
  case const_hash("ib_sdiv"): return m_builder->CreateSDiv(a, b);
  case const_hash("ib_udiv"): return m_builder->CreateUDiv(a, b);
  default: throw std::runtime_error("Unknown binary integer primitive "s + primitive);
  }
}

static inline auto vals_to_llvm(const vector<Val>& in) -> vector<llvm::Value*> {
  auto out = vector<llvm::Value*>{};
  for (const auto& i : in)
    out.push_back(i.llvm);

  return out;
}

auto Compiler::primitive(Fn* fn, const vector<Val>& args, const vector<ty::Type>& types) -> optional<Val> {
  if (fn->extern_decl())
    return m_builder->CreateCall(declare(*fn), vals_to_llvm(args));

  if (!fn->primitive())
    return {};

  auto primitive = get<string>(fn->fn_body());

  if (primitive == "ptrto")
    return args.at(0);
  if (primitive == "slice_malloc") {
    auto base_ty_type = types.at(0).ensure_ptr_base();
    auto* base_type = llvm_type(base_ty_type);
    auto slice_size = args.at(1);

    return create_malloc(base_type, slice_size, "sl.ctor.malloc");
  }
  if (primitive == "default_init") {
    auto base_type = types.at(0).ensure_mut_base();
    m_builder->CreateStore(default_init(base_type), args.at(0));

    return args.at(0);
  }
  if (primitive == "set_at") {
    auto* result_type = llvm_type(types[0].without_mut().ensure_ptr_base());
    llvm::Value* value = args.at(2);
    llvm::Value* base = m_builder->CreateGEP(result_type, args.at(0).llvm, args.at(1).llvm, "p.set_at.gep");
    m_builder->CreateStore(value, base);
    return llvm::UndefValue::get(m_builder->getVoidTy());
  }
  if (primitive == "get_at") {
    auto* result_type = llvm_type(types[0].without_mut().ensure_ptr_base());
    llvm::Value* base = args.at(0);
    base = m_builder->CreateGEP(result_type, base, args.at(1).llvm, "p.get_at.gep");
    return base;
  }
  if (primitive.starts_with("ib_"))
    return int_bin_primitive(primitive, args);
  throw std::runtime_error("Unknown primitive "s + primitive);
}

template <> auto Compiler::expression(ast::CallExpr& expr) -> Val {
  if (expr.name == "->") // TODO(rymiel): Magic value?
    return direct_call_operator(expr);

  auto* selected = expr.selected_overload;
  llvm::Function* llvm_fn = nullptr;

  vector<Val> args{};
  vector<ty::Type> arg_types{};
  vector<Val> llvm_args{};

  for (auto& i : expr.args) {
    auto arg = body_expression(*i);
    args.push_back(arg);
    llvm_args.emplace_back(arg.llvm);
    arg_types.push_back(i->ensure_ty());
  }

  Val val{nullptr};

  auto prim = primitive(selected, llvm_args, arg_types);
  if (prim.has_value()) {
    val = *prim;
  } else {
    llvm_fn = declare(*selected);
    val = m_builder->CreateCall(llvm_fn, vals_to_llvm(llvm_args));
  }

  if (auto ty = expr.val_ty(); ty.has_value() && !ty->is_trivially_destructible())
    make_temporary_in_scope(val, expr, "tmp");

  return val;
}

template <> auto Compiler::expression(ast::AssignExpr& expr) -> Val {
  if (const auto* target_var = dyn_cast<ast::VarExpr>(expr.target.raw_ptr())) {
    auto* in_scope = m_scope.find(target_var->name);
    yume_assert(in_scope != nullptr, "Variable "s + target_var->name + " is not in scope");

    destruct_indirect(*this, *in_scope);

    auto expr_val = body_expression(*expr.value);
    if (expr_val.scope != nullptr && expr_val.scope->owning)
      expr_val.scope->owning = false;

    m_builder->CreateStore(expr_val, in_scope->value.llvm);
    return expr_val;
  }
  if (auto* field_access = dyn_cast<ast::FieldAccessExpr>(expr.target.raw_ptr())) {
    auto& field_base = field_access->base;
    const auto [struct_base, base] = [&]() -> tuple<ty::Type, Val> {
      if (field_base.has_value()) {
        auto base = body_expression(*field_base);
        auto struct_base = field_access->base->ensure_ty();
        return {struct_base, base};
      } // TODO(rymiel): revisit
      if (!isa<ast::CtorDecl>(m_current_fn->ast()))
        throw std::logic_error("Field access without a base is only available in constructors");

      auto [value, ast, owning] = *m_scope_ctor;
      return {ast.ensure_ty().known_mut(), value};
    }();

    yume_assert(struct_base.is_mut(), "Cannot assign into field of immutable structure");

    const string base_name = field_access->field;
    const int base_offset = field_access->offset;

    auto expr_val = body_expression(*expr.value);
    auto* struct_type = llvm_type(struct_base.ensure_mut_base().base_cast<ty::Struct>());

    yume_assert(base_offset >= 0, "Field access has unknown offset into struct");

    auto* gep = m_builder->CreateStructGEP(struct_type, base, base_offset, "s.sf."s + base_name);
    m_builder->CreateStore(expr_val, gep);
    return expr_val;
  }
  throw std::runtime_error("Can't assign to target "s + expr.target->kind_name());
}

template <> auto Compiler::expression(ast::LambdaExpr& expr) -> Val {
  auto fn = Fn{&expr, m_current_fn->member, m_current_fn->self_ty};
  fn.fn_ty = expr.ensure_ty().base_cast<ty::Function>();

  declare(fn);
  expr.llvm_fn = fn.llvm;

  Val fn_bundle = llvm::UndefValue::get(fn.fn_ty->memo());
  fn_bundle = m_builder->CreateInsertValue(fn_bundle, fn.llvm, 0);

  auto* llvm_closure_ty = fn.fn_ty->closure_memo();
  auto* llvm_closure = m_builder->CreateAlloca(llvm_closure_ty);

  // Capture every closured local in the closure object
  for (const auto& i : llvm::enumerate(llvm::zip(expr.closured_names, expr.closured_nodes))) {
    auto [name, ast_arg] = i.value();
    auto type = ast_arg->ensure_ty();
    auto* val = m_scope.find(name);
    yume_assert(val != nullptr, "Captured variable not found in outer scope");
    yume_assert(val->ast.ensure_ty() == type, "Capture variable does not match expected type: wanted "s + type.name() +
                                                  ", but got " + val->ast.ensure_ty().name());

    m_builder->CreateStore(val->value,
                           m_builder->CreateConstInBoundsGEP2_32(llvm_closure_ty, llvm_closure, 0, i.index()));
  }

  // TODO(LLVM MIN >= 15): BitCast obsoleted by opaque pointers
  fn_bundle =
      m_builder->CreateInsertValue(fn_bundle, m_builder->CreateBitCast(llvm_closure, m_builder->getInt8PtrTy()), 1);

  auto saved_scope = m_scope;
  auto* saved_insert_point = m_builder->GetInsertBlock();
  auto* saved_fn = m_current_fn;

  setup_fn_base(fn);

  // TODO(LLVM MIN >= 15): BitCast obsoleted by opaque pointers
  const Val closure_val = m_builder->CreateLoad(
      llvm_closure_ty, m_builder->CreateBitCast(fn.llvm->arg_begin(), llvm_closure_ty->getPointerTo()), "closure");

  // Add local variables for every captured variable
  for (const auto& i : llvm::enumerate(llvm::zip(expr.closured_names, expr.closured_nodes))) {
    auto [name, ast_arg] = i.value();
    auto type = ast_arg->ensure_ty();
    const Val arg = m_builder->CreateExtractValue(closure_val, i.index());
    expose_parameter_as_local(type, name, *ast_arg, arg);
  }

  body_statement(expr.body);
  if (m_builder->GetInsertBlock()->getTerminator() == nullptr && !fn.fn_ty->m_ret.has_value())
    m_builder->CreateRetVoid();

  m_scope = saved_scope;
  m_builder->SetInsertPoint(saved_insert_point);
  m_current_fn = saved_fn;

  return fn_bundle;
}

auto Compiler::direct_call_operator(ast::CallExpr& expr) -> Val {
  yume_assert(expr.args.size() > 1, "Direct call must have at least 1 argument");
  auto& base_expr = *expr.args[0];

  auto call_target_ty = base_expr.ensure_ty();
  yume_assert(call_target_ty.base_isa<ty::Function>(), "Direct call target must be a function type");
  auto* llvm_fn_ty = call_target_ty.base_cast<ty::Function>()->fn_memo();
  auto* llvm_fn_bundle_ty = llvm_type(call_target_ty.without_mut());

  auto base = body_expression(base_expr);
  if (call_target_ty.is_mut())
    base = m_builder->CreateLoad(llvm_fn_bundle_ty, base.llvm, "indir.fnptr.deref");

  auto* llvm_fn_ptr = m_builder->CreateExtractValue(base, 0, "fnptr.fn");
  auto* llvm_fn_closure = m_builder->CreateExtractValue(base, 1, "fnptr.closure");

  vector<llvm::Value*> args{};
  args.reserve(expr.args.size() - 1);
  args.push_back(llvm_fn_closure);

  for (auto& i : llvm::drop_begin(expr.args))
    args.push_back(body_expression(*i));

  return m_builder->CreateCall(llvm_fn_ty, llvm_fn_ptr, args,
                               llvm_fn_ty->getReturnType()->isVoidTy() ? "" : "indir.call");
}

template <> auto Compiler::expression(ast::CtorExpr& expr) -> Val {
  auto type = expr.ensure_ty();
  if (type.without_mut().base_isa<ty::Struct>()) {
    // TODO(rymiel): #4 determine what kind of allocation must be done, and if at all. It'll probably require a
    // complicated semantic step to determine object lifetime, which would probably be evaluated before compilation of
    // these expressions.

    //// Heap allocation
    // auto* llvm_struct_type = llvm_type(*struct_type);
    // llvm::Value* alloc = nullptr;
    // auto* alloc_size = llvm::ConstantExpr::getSizeOf(llvm_struct_type);
    // alloc = llvm::CallInst::CreateMalloc(m_builder->GetInsertBlock(), m_builder->getInt64Ty(), llvm_struct_type,
    //                                      alloc_size, nullptr, nullptr, "s.ctor.malloc");
    // alloc = m_builder->Insert(alloc);

    //// Stack allocation
    // alloc = m_builder->CreateAlloca(llvm_struct_type, 0, nullptr, "s.ctor.alloca");

    //// Value allocation
    auto* selected_ctor_overload = expr.selected_overload;

    auto* llvm_fn = declare(*selected_ctor_overload);
    vector<llvm::Value*> llvm_args{};
    for (auto& i : expr.args) {
      auto arg = body_expression(*i);
      llvm_args.push_back(arg.llvm);
    }
    Val base_value = m_builder->CreateCall(llvm_fn, llvm_args);

    //// Heap allocation
    // if (mut) {
    //   m_builder->CreateStore(base_value, alloc);
    //   base_value = alloc;
    // }

    return base_value;
  }
  if (auto int_type = type.without_mut().try_as<ty::Int>()) {
    yume_assert(expr.args.size() == 1, "Numeric cast can only contain a single argument");
    auto& cast_from = expr.args[0];
    yume_assert(cast_from->ensure_ty().without_mut().base_isa<ty::Int>(), "Numeric cast must convert from int");
    auto base = body_expression(*cast_from);
    if (cast_from->ensure_ty().without_mut().base_cast<ty::Int>()->is_signed()) {
      return m_builder->CreateSExtOrTrunc(base, llvm_type(*int_type));
    }
    return m_builder->CreateZExtOrTrunc(base, llvm_type(*int_type));
  }

  //// Stack allocation of slice
  // if (auto* const_value = dyn_cast<llvm::ConstantInt>(slice_size.llvm())) {
  //   auto slice_size_val = const_value->getLimitedValue();
  //   auto* array_type = ArrayType::get(base_type, slice_size_val);
  //   auto* array_alloc = m_builder->CreateAlloca(array_type, nullptr, "sl.ctor.alloc");

  //   auto* data_ptr = m_builder->CreateBitCast(array_alloc, base_type->getPointerTo(), "sl.ctor.ptr");
  //   llvm::Value* slice_inst = llvm::UndefValue::get(llvm_slice_type);
  //   slice_inst = m_builder->CreateInsertValue(slice_inst, data_ptr, 0);
  //   slice_inst = m_builder->CreateInsertValue(slice_inst, m_builder->getInt64(slice_size_val), 1);
  //   slice_inst->setName("sl.ctor.inst");

  //   return slice_inst;
  // }

  // TODO(rymiel): the commented-out block above stack-allocates a slice constructor if its size can be determined
  // trivially. However, since it references stack memory, a slice allocated this way could never be feasibly returned
  // or passed into a function which stores a reference to it, etc. The compiler currently does nothing resembling
  // "escape analysis", however something like that might be needed to perform an optimization like shown above.

  // TODO(rymiel): Note that also a slice could be stack-allocated even if its size *wasn't* known at compile time,
  // however, I simply didn't know how to do that when i wrote the above snippet. But, since its problematic anyway,
  // it remains unfixed (and commented out); revisit later.

  // TODO(rymiel): A large slice may be unfeasible to be stack-allocated anyway, so in addition to the above points,
  // slice size could also be a consideration. Perhaps we don't *want* to stack-allocate unknown-sized slices as they
  // may be absurdly huge in size and cause stack overflow.
  // Related: #4

  // NOTE: The above comments are now largely irrelevant as slice allocation is sort-of performed in-source, with the
  // __builtin_slice_malloc primitive. For the above to apply, the compiler must more invasively track the lifetime of
  // slices, and skip the constructor entirely, or something like that

  throw std::runtime_error("Can't construct non-struct, non-integer type");
}

auto Compiler::ptr_bitsize() -> unsigned int { return m_module->getDataLayout().getPointerSizeInBits(); }

auto Compiler::create_malloc(llvm::Type* base_type, Val slice_size, string_view name) -> Val {
  auto* size_type = m_builder->getIntNTy(ptr_bitsize());
  slice_size = m_builder->CreateSExtOrTrunc(slice_size, size_type);
  Val alloc_size = llvm::ConstantExpr::getTrunc(llvm::ConstantExpr::getSizeOf(base_type), size_type);

  alloc_size = m_builder->CreateMul(slice_size, alloc_size, "mallocsize");

  // prototype malloc as "void *malloc(size_t)"
  llvm::FunctionCallee malloc_func = m_module->getOrInsertFunction("malloc", m_builder->getInt8PtrTy(), size_type);
  auto* m_call = m_builder->CreateCall(malloc_func, alloc_size.llvm, "malloccall");
  Val result = m_builder->CreateBitCast(m_call, base_type->getPointerTo(), name);

  m_call->setTailCall();
  if (auto* fn = dyn_cast<llvm::Function>(malloc_func.getCallee())) {
    m_call->setCallingConv(fn->getCallingConv());
    if (!fn->returnDoesNotAlias())
      fn->setReturnDoesNotAlias();
  }

  return result;
}

auto Compiler::create_malloc(llvm::Type* base_type, uint64_t slice_size, string_view name) -> Val {
  return create_malloc(base_type, m_builder->getIntN(ptr_bitsize(), slice_size), name);
}

template <> auto Compiler::expression(ast::SliceExpr& expr) -> Val {
  auto* slice_size = m_builder->getIntN(ptr_bitsize(), expr.args.size());

  yume_assert(expr.ensure_ty().is_slice(), "Slice expression must contain slice type");
  auto* slice_type = llvm_type(expr.ensure_ty());
  auto* base_type =
      llvm_type(expr.ensure_ty().base_cast<ty::Struct>()->fields().at(0)->ensure_ty().ensure_ptr_base()); // ???

  const Val data_ptr = create_malloc(base_type, slice_size, "sl.ctor.malloc");

  unsigned j = 0;
  for (auto& i : expr.args)
    m_builder->CreateStore(body_expression(*i), m_builder->CreateConstInBoundsGEP1_32(base_type, data_ptr, j++));

  Val slice_inst = llvm::UndefValue::get(slice_type);
  slice_inst = m_builder->CreateInsertValue(slice_inst, data_ptr, 0);
  slice_inst = m_builder->CreateInsertValue(slice_inst, slice_size, 1);

  return slice_inst;
}

template <> auto Compiler::expression(ast::FieldAccessExpr& expr) -> Val {
  optional<Val> base;
  optional<ty::Type> base_type;
  if (expr.base.has_value()) {
    base = body_expression(*expr.base);
    base_type = expr.base->ensure_ty().mut_base();
  } else {
    yume_assert(m_scope_ctor.has_value(), "Cannot access field without receiver outside of a constructor");
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): clang-tidy doesn't accept yume_assert as an assertion
    base = m_scope_ctor->value;
    base_type = m_current_fn->self_ty;
  }

  const string base_name = expr.field;
  const int base_offset = expr.offset;

  if (!expr.ensure_ty().is_mut())
    return m_builder->CreateExtractValue(*base, base_offset, "s.field.nm."s + base_name);

  yume_assert(base_type.has_value(), "Cannot access field of unknown type");
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): clang-tidy doesn't accept yume_assert as an assertion
  return m_builder->CreateStructGEP(llvm_type(base_type.value()), *base, base_offset, "s.field.m."s + base_name);
}

template <> auto Compiler::expression(ast::ImplicitCastExpr& expr) -> Val {
  auto target_ty = expr.ensure_ty();
  auto current_ty = expr.base->ensure_ty();
  Val base = body_expression(*expr.base);

  if (expr.conversion.dereference) {
    yume_assert(current_ty.is_mut(), "Source type must be mutable when implicitly derefencing");
    current_ty = current_ty.ensure_mut_base();
    base.llvm = m_builder->CreateLoad(llvm_type(current_ty), base, "ic.deref");
  }

  if (expr.conversion.kind == ty::Conv::Int) {
    return m_builder->CreateIntCast(base, llvm_type(target_ty), current_ty.base_cast<ty::Int>()->is_signed(), "ic.int");
  }

  if (expr.conversion.kind == ty::Conv::Kind::FnPtr) {
    yume_assert(current_ty.base_isa<ty::Function>(), "fnptr conversion source must be a function type");
    yume_assert(isa<ast::LambdaExpr>(*expr.base), "fnptr conversion source must be a lambda");
    yume_assert(target_ty.base_isa<ty::Function>(), "fnptr conversion target must be a function type");
    auto& lambda_expr = cast<ast::LambdaExpr>(*expr.base);

    auto fn = Fn{&lambda_expr, nullptr};
    fn.fn_ty = target_ty.base_cast<ty::Function>();

    declare(fn);

    auto saved_scope = m_scope;
    auto* saved_insert_point = m_builder->GetInsertBlock();
    auto* saved_fn = m_current_fn;

    setup_fn_base(fn);

    body_statement(lambda_expr.body);
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr && !fn.fn_ty->m_ret.has_value())
      m_builder->CreateRetVoid();

    m_scope = saved_scope;
    m_builder->SetInsertPoint(saved_insert_point);
    m_current_fn = saved_fn;

    return fn.llvm;
  }

  return base;
}

void Compiler::write_object(const char* filename, bool binary) {
  auto dest = open_file(filename);

  llvm::legacy::PassManager pass;
  auto file_type = binary ? llvm::CGFT_ObjectFile : llvm::CGFT_AssemblyFile;

  if (m_targetMachine->addPassesToEmitFile(pass, *dest, nullptr, file_type)) {
    errs() << "TargetMachine can't emit a file of this type";
    throw std::exception();
  }

  pass.run(*m_module);
  dest->flush();
}

void Compiler::body_statement(ast::Stmt& stat) {
  const ASTStackTrace guard("Codegen: "s + stat.kind_name() + " statement", stat);
  return CRTPWalker::body_statement(stat);
}

auto Compiler::body_expression(ast::Expr& expr) -> Val {
  const ASTStackTrace guard("Codegen: "s + expr.kind_name() + " expression", expr);
  return CRTPWalker::body_expression(expr);
}
} // namespace yume
