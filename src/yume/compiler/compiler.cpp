#include "compiler.hpp"
#include "ast/ast.hpp"
#include "diagnostic/errors.hpp"
#include "semantic/type_walker.hpp"
#include "ty/compatibility.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include "vals.hpp"
#include <algorithm>
#include <bits/ranges_algo.h>
#include <exception>
#include <functional>
#include <llvm/ADT/Twine.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
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
#if __has_include(<llvm/MC/TargetRegistry.h>)
#include <llvm/MC/TargetRegistry.h>
#else
#include <llvm/Support/TargetRegistry.h>
#endif
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace yume {
Compiler::Compiler(vector<SourceFile> source_files)
    : m_sources(move(source_files)), m_walker(std::make_unique<semantic::TypeWalker>(*this)) {
  m_context = std::make_unique<llvm::LLVMContext>();
  m_module = std::make_unique<llvm::Module>("yume", *m_context);
  m_builder = std::make_unique<llvm::IRBuilder<>>(*m_context);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();
  string error;
  string targetTriple = llvm::sys::getDefaultTargetTriple();
  const auto* target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

  if (target == nullptr) {
    llvm::errs() << error;
    throw std::exception();
  }
  const char* cpu = "generic";
  const char* feat = "";

  llvm::TargetOptions opt;
  m_targetMachine = unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(targetTriple, cpu, feat, opt, llvm::Reloc::Model::PIC_));

  m_module->setDataLayout(m_targetMachine->createDataLayout());
  m_module->setTargetTriple(targetTriple);
}

void Compiler::run() {
  for (const auto& source : m_sources)
    for (auto& i : source.m_program->body())
      decl_statement(i, nullptr, source.m_program.get());

  // First pass: only convert structs
  for (auto& st : m_structs)
    walk_types(&st);

  // Second pass: only convert function parameters
  for (auto& fn : m_fns)
    walk_types(&fn);

  // Third pass: convert everything else, but only when instantiated
  m_walker->m_in_depth = true;

  Fn* main_fn = nullptr;
  for (auto& fn : m_fns)
    if (fn.name() == "main")
      main_fn = &fn;

  if (main_fn == nullptr)
    throw std::logic_error("Program is missing a `main` function!"); // Related: #10
  declare(*main_fn, false);

  while (!m_decl_queue.empty()) {
    auto* next = m_decl_queue.front();
    m_decl_queue.pop();
    define(*next);
  }
}

void Compiler::walk_types(DeclLike decl_like) {
  std::visit(DeclLikeVisitor{[&](Fn* fn) {
                               m_walker->m_current_fn = fn;
                               m_walker->body_statement(fn->ast());
                               m_walker->m_current_fn = nullptr;
                             },
                             [&](Struct* st) {
                               m_walker->m_current_struct = st;
                               m_walker->body_statement(st->ast());
                               m_walker->m_current_struct = nullptr;
                             }},
             decl_like);
}

auto Compiler::create_struct(ast::StructDecl& s_decl, const optional<string>& name_override) -> unique_ptr<ty::Struct> {
  auto fields = vector<const ast::TypeName*>();
  fields.reserve(s_decl.fields().size());
  for (const auto& f : s_decl.fields())
    fields.push_back(&f);

  return std::make_unique<ty::Struct>(name_override.value_or(s_decl.name()), fields);
};

auto Compiler::decl_statement(ast::Stmt& stmt, ty::Type* parent, ast::Program* member) -> DeclLike {
  if (auto* fn_decl = dyn_cast<ast::FnDecl>(&stmt)) {
    vector<unique_ptr<ty::Generic>> type_args{};
    std::map<string, const ty::Type*> subs{};
    for (auto& i : fn_decl->type_args()) {
      auto& gen = type_args.emplace_back(std::make_unique<ty::Generic>(i));
      subs.try_emplace(i, gen.get());
    }
    auto& fn = m_fns.emplace_back(*fn_decl, parent, member, move(subs), move(type_args));

    return &fn;
  }
  if (auto* s_decl = dyn_cast<ast::StructDecl>(&stmt)) {
    auto i_ty = m_types.known.insert({s_decl->name(), create_struct(*s_decl)});

    vector<unique_ptr<ty::Generic>> type_args{};
    std::map<string, const ty::Type*> subs{};
    for (auto& i : s_decl->type_args()) {
      auto& gen = type_args.emplace_back(std::make_unique<ty::Generic>(i));
      subs.try_emplace(i, gen.get());
    }
    auto& st = m_structs.emplace_back(*s_decl, i_ty.first->second.get(), member, std::move(subs), std::move(type_args));

    if (st.type_args.empty())
      for (auto& f : s_decl->body().body())
        decl_statement(f, st.type(), member);

    return &st;
  }

  return {};
}

auto Compiler::llvm_type(const ty::Type& type) -> llvm::Type* {
  if (const auto* int_type = dyn_cast<ty::Int>(&type))
    return llvm::Type::getIntNTy(*m_context, int_type->size());
  if (const auto* qual_type = dyn_cast<ty::Qual>(&type))
    return llvm_type(qual_type->base())->getPointerTo();
  if (const auto* ptr_type = dyn_cast<ty::Ptr>(&type)) {
    switch (ptr_type->qualifier()) {
    case Qualifier::Ptr: return llvm::PointerType::getUnqual(llvm_type(ptr_type->base()));
    case Qualifier::Slice: {
      auto args = vector<llvm::Type*>{};
      args.push_back(llvm::PointerType::getUnqual(llvm_type(ptr_type->base())));
      args.push_back(llvm::Type::getInt64Ty(*m_context));
      return llvm::StructType::get(*m_context, args);
    }
    default: llvm_unreachable("Ptr type cannot hold this qualifier");
    }
  }
  if (const auto* struct_type = dyn_cast<ty::Struct>(&type)) {
    auto* memo = struct_type->memo();
    if (memo == nullptr) {
      auto fields = vector<llvm::Type*>{};
      for (const auto& i : struct_type->fields())
        fields.push_back(llvm_type(*i.type().val_ty()));

      memo = llvm::StructType::create(*m_context, fields, "_"s + struct_type->name());
      struct_type->memo(memo);
    }

    return memo;
  }

  return llvm::Type::getVoidTy(*m_context);
}

void Compiler::destruct(Val val, const ty::Type& type) {
  if (type.is_mut()) {
    const auto& deref_type = *type.qual_base();
    return destruct(m_builder->CreateLoad(llvm_type(deref_type), val), deref_type);
  }
  if (const auto* ptr_type = dyn_cast<ty::Ptr>(&type)) {
    if (ptr_type->has_qualifier(Qualifier::Slice)) {
      auto* ptr = m_builder->CreateExtractValue(val, 0, "sl.ptr.free");
      auto* free = llvm::CallInst::CreateFree(ptr, m_builder->GetInsertBlock());
      m_builder->Insert(free);
    }
  }
}

auto Compiler::default_init(const ty::Type& type) -> Val {
  if (const auto* int_type = dyn_cast<ty::Int>(&type))
    return m_builder->getIntN(int_type->size(), 0);
  if (isa<ty::Qual>(&type))
    throw std::runtime_error("Cannot default-initialize a reference");
  if (const auto* ptr_type = dyn_cast<ty::Ptr>(&type)) {
    switch (ptr_type->qualifier()) {
    default: llvm_unreachable("Ptr type cannot hold this qualifier");
    case Qualifier::Ptr:
      llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(llvm_type(ptr_type->base())));
      break;
    case Qualifier::Slice:
      auto* ptr_member_type = llvm::PointerType::getUnqual(llvm_type(ptr_type->base()));
      auto* struct_type = cast<llvm::StructType>(llvm_type(type));
      return llvm::ConstantStruct::get(struct_type, llvm::ConstantPointerNull::get(ptr_member_type),
                                       m_builder->getInt64(0));
    }
  }
  if (const auto* struct_type = dyn_cast<ty::Struct>(&type)) {
    auto* llvm_ty = cast<llvm::StructType>(llvm_type(type));
    llvm::Value* val = llvm::UndefValue::get(llvm_ty);
    int i = 0;
    for (const auto& field : struct_type->fields()) {
      val = m_builder->CreateInsertValue(val, default_init(*field.type().val_ty()), i++);
    }

    return val;
  }

  throw std::runtime_error("Cannot default-initialize "s + type.name());
}

auto Compiler::declare(Fn& fn, bool mangle) -> llvm::Function* {
  if (fn.m_llvm_fn != nullptr)
    return fn.m_llvm_fn;
  // Skip primitive definitions, unless they are actually external functions (i.e. printf)
  if (fn.ast().primitive() && mangle)
    return nullptr;
  const auto& fn_decl = fn.m_ast_decl;
  auto* llvm_ret_type = llvm::Type::getVoidTy(*m_context);
  auto llvm_args = vector<llvm::Type*>{};
  if (fn_decl.ret())
    llvm_ret_type = llvm_type(*fn_decl.ret()->get().val_ty());

  for (const auto& i : fn_decl.args())
    llvm_args.push_back(llvm_type(*i.val_ty()));

  llvm::FunctionType* fn_t = llvm::FunctionType::get(llvm_ret_type, llvm_args, fn_decl.varargs());

  string name = fn_decl.name();
  if (mangle)
    name = mangle_name(fn);

  auto linkage = mangle ? llvm::Function::InternalLinkage : llvm::Function::ExternalLinkage;
  auto* llvm_fn = llvm::Function::Create(fn_t, linkage, name, m_module.get());
  fn.m_llvm_fn = llvm_fn;

  int arg_i = 0;
  for (auto& arg : llvm_fn->args()) {
    arg.setName("arg."s + fn_decl.args()[arg_i].name());
    arg_i++;
  }

  // Primitive definitions that got this far are actually external functions; declare its prototype but not its
  // body
  if (!fn_decl.primitive()) {
    m_decl_queue.push(&fn);
    m_walker->m_current_fn = &fn;
    m_walker->body_statement(fn.ast());
  }
  return llvm_fn;
}

template <> void Compiler::statement(const ast::Compound& stat) {
  for (const auto& i : stat.body())
    body_statement(i);
}

static inline auto is_trivially_destructible(const ty::Type* type) -> bool {
  if (isa<ty::Int>(type))
    return true;

  if (isa<ty::Qual>(type))
    return is_trivially_destructible(type->qual_base());

  if (const auto* ptr_type = dyn_cast<ty::Ptr>(type))
    return !ptr_type->has_qualifier(Qualifier::Slice);

  if (const auto* struct_type = dyn_cast<ty::Struct>(type))
    return std::ranges::all_of(struct_type->fields(), &is_trivially_destructible, &ast::TypeName::get_val_ty);

  // A generic or something, shouldn't occur
  throw std::logic_error("Cannot check if "s + type->name() + " is trivially destructible");
}

void Compiler::destruct_all_in_scope(ast::FnDecl& scope_parent) {
  yume_assert(std::holds_alternative<ast::Compound>(scope_parent.body()), "Primitives don't have scope");

  for (const auto& [k, v] : m_scope)
    if (isa<ast::VarDecl>(v.ast) && v.owning && !is_trivially_destructible(v.ast.val_ty()))
      destruct(v.value, *v.ast.val_ty());
}

void Compiler::define(Fn& fn) {
  m_current_fn = &fn;
  m_scope.clear();
  auto* decl_bb = llvm::BasicBlock::Create(*m_context, "decl", fn);
  auto* bb = llvm::BasicBlock::Create(*m_context, "entry", fn);
  m_builder->SetInsertPoint(bb);
  m_current_fn->m_decl_bb = decl_bb;

  // Allocate local variables for each parameter
  for (auto [arg, ast_arg] : llvm::zip(fn.llvm()->args(), fn.ast().args())) {
    const auto& type = *ast_arg.val_ty();
    auto name = ast_arg.name();
    llvm::Value* alloc = nullptr;
    if (const auto* qual_type = dyn_cast<ty::Qual>(&type)) {
      if (qual_type->is_mut()) {
        m_scope.insert({name, {.value = &arg, .ast = ast_arg, .owning = false}}); // We don't own parameters
        continue;
      }
    }
    alloc = m_builder->CreateAlloca(llvm_type(type), nullptr, name);
    m_builder->CreateStore(&arg, alloc);
    m_scope.insert({name, {.value = alloc, .ast = ast_arg, .owning = false}}); // We don't own parameters
  }

  if (const auto* body = get_if<ast::Compound>(&fn.body()); body != nullptr) {
    statement(*body);
  }

  if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
    destruct_all_in_scope(fn.ast());
    m_builder->CreateRetVoid();
  }

  m_builder->SetInsertPoint(decl_bb);
  m_builder->CreateBr(bb);
  verifyFunction(*fn, &llvm::errs());
}

template <> void Compiler::statement(const ast::WhileStmt& stat) {
  auto* test_bb = llvm::BasicBlock::Create(*m_context, "while.test", *m_current_fn);
  auto* head_bb = llvm::BasicBlock::Create(*m_context, "while.head", *m_current_fn);
  auto* merge_bb = llvm::BasicBlock::Create(*m_context, "while.merge", *m_current_fn);
  m_builder->CreateBr(test_bb);
  m_builder->SetInsertPoint(test_bb);
  auto cond_value = body_expression(stat.cond());
  m_builder->CreateCondBr(cond_value, head_bb, merge_bb);
  m_builder->SetInsertPoint(head_bb);
  body_statement(stat.body());
  m_builder->CreateBr(test_bb);
  m_builder->SetInsertPoint(merge_bb);
}

template <> void Compiler::statement(const ast::IfStmt& stat) {
  auto* merge_bb = llvm::BasicBlock::Create(*m_context, "if.cont", *m_current_fn);
  auto* next_test_bb = llvm::BasicBlock::Create(*m_context, "if.test", *m_current_fn, merge_bb);
  bool all_terminated = true;
  m_builder->CreateBr(next_test_bb);

  const auto& clauses = stat.clauses();
  for (const auto& clause : clauses) {
    m_builder->SetInsertPoint(next_test_bb);
    auto* body_bb = llvm::BasicBlock::Create(*m_context, "if.then", *m_current_fn, merge_bb);
    next_test_bb = llvm::BasicBlock::Create(*m_context, "if.test", *m_current_fn, merge_bb);
    auto condition = body_expression(clause.cond());
    m_builder->CreateCondBr(condition, body_bb, next_test_bb);
    m_builder->SetInsertPoint(body_bb);
    statement(clause.body());
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      all_terminated = false;
      m_builder->CreateBr(merge_bb);
    }
  }

  if (stat.else_clause().has_value()) {
    next_test_bb->setName("if.else");
    m_builder->SetInsertPoint(next_test_bb);
    statement(*stat.else_clause());
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      all_terminated = false;
      m_builder->CreateBr(merge_bb);
    }
  } else {
    // HACK:  For if statements with no else clause, it creates an empty `if.test` block which only contains a single
    // `br` instruction to the merge point.
    m_builder->SetInsertPoint(next_test_bb);
    m_builder->CreateBr(merge_bb);
  }

  if (all_terminated)
    merge_bb->eraseFromParent();
  else
    m_builder->SetInsertPoint(merge_bb);
}

template <> void Compiler::statement(const ast::ReturnStmt& stat) {
  const auto* ret_ty = m_current_fn->ast().val_ty();
  bool returns_mut = ret_ty != nullptr && ret_ty->is_mut();
  InScope* reset_owning = nullptr;

  if (stat.expr().has_value()) {
    if (auto* var = dyn_cast<ast::VarExpr>(&stat.expr().value().get())) {
      auto& in_scope = m_scope.at(var->name());
      if (in_scope.owning) {
        in_scope.owning = false; // Returning a local variable also gives up ownership of it
        reset_owning = &in_scope;
      }
    }

    destruct_all_in_scope(m_current_fn->ast());

    if (reset_owning != nullptr)
      reset_owning->owning = true; // The local variable may not be returned in all code paths, so reset its ownership

    auto val = body_expression(stat.expr().value(), returns_mut);
    m_builder->CreateRet(val);

    return;
  }

  destruct_all_in_scope(m_current_fn->ast());
  m_builder->CreateRetVoid();
}

template <> void Compiler::statement(const ast::VarDecl& stat) {
  // Locals are currently always mut, get the base type instead
  // TODO: revisit, probably extract logic
  auto* var_type = llvm_type(*stat.val_ty()->qual_base());

  auto* current_block = m_builder->GetInsertBlock();

  m_builder->SetInsertPoint(m_current_fn->m_decl_bb);
  auto* alloc = m_builder->CreateAlloca(var_type, nullptr, stat.name());
  m_builder->SetInsertPoint(current_block);

  auto expr_val = body_expression(stat.init());
  m_builder->CreateStore(expr_val, alloc);
  m_scope.insert({stat.name(), {.value = alloc, .ast = stat, .owning = true}});
}

/// Assert that this expression isn't asked to be mutable.
static void not_mut(const string& message, bool mut) {
  if (mut)
    throw std::runtime_error(message + " cannot be mutable!");
}

template <> auto Compiler::expression(const ast::NumberExpr& expr, bool mut) -> Val {
  not_mut("number constant", mut);

  auto val = expr.val();
  if (expr.val_ty() == m_types.int64().s_ty)
    return m_builder->getInt64(val);
  return m_builder->getInt32(val);
}

template <> auto Compiler::expression(const ast::CharExpr& expr, bool mut) -> Val {
  not_mut("character constant", mut);

  return m_builder->getInt8(expr.val());
}

template <> auto Compiler::expression(const ast::BoolExpr& expr, bool mut) -> Val {
  not_mut("boolean constant", mut);

  return m_builder->getInt1(expr.val());
}

template <> auto Compiler::expression(const ast::StringExpr& expr, bool mut) -> Val {
  not_mut("string constant", mut);

  auto val = expr.val();

  vector<llvm::Constant*> chars(val.length());
  for (unsigned int i = 0; i < val.size(); i++)
    chars[i] = m_builder->getInt8(val[i]);

  chars.push_back(m_builder->getInt8(0));
  auto* stringType = llvm::ArrayType::get(m_builder->getInt8Ty(), chars.size());
  auto* init = llvm::ConstantArray::get(stringType, chars);
  auto* global =
      new llvm::GlobalVariable(*m_module, stringType, true, llvm::GlobalVariable::PrivateLinkage, init, ".str");
  return llvm::ConstantExpr::getBitCast(global, m_builder->getInt8PtrTy(0));
}

template <> auto Compiler::expression(const ast::VarExpr& expr, bool mut) -> Val {
  auto* val = m_scope.at(expr.name()).value.llvm();
  // Function arguments can act as locals, but they can be immutable, but still behind a reference (alloca)
  if (!mut)
    return m_builder->CreateLoad(llvm_type(expr.val_ty()->without_qual()), val);

  return val;
}

/// A constexpr-friendly simple string hash, for simple switches with string cases
static auto constexpr const_hash(char const* input) -> unsigned {
  return *input != 0 ? static_cast<unsigned int>(*input) + 33 * const_hash(input + 1) : 5381; // NOLINT
}

auto Compiler::int_bin_primitive(const string& primitive, const vector<llvm::Value*>& args) -> Val {
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
  case const_hash("ib_srem"): return m_builder->CreateSRem(a, b);
  case const_hash("ib_urem"): return m_builder->CreateURem(a, b);
  case const_hash("ib_sdiv"): return m_builder->CreateSDiv(a, b);
  case const_hash("ib_udiv"): return m_builder->CreateUDiv(a, b);
  default: throw std::runtime_error("Unknown binary integer primitive ib_"s + primitive);
  }
}

auto Compiler::primitive(Fn* fn, const vector<llvm::Value*>& args, const vector<const ty::Type*>& types,
                         const ty::Type* ret_ty) -> optional<Val> {
  if (!fn->ast().primitive())
    return {};

  auto primitive = get<string>(fn->body());
  bool returns_mut = ret_ty != nullptr && ret_ty->is_mut();

  if (primitive == "libc")
    return m_builder->CreateCall(fn->declaration(*this, false), args);
  if (primitive == "ptrto")
    return args.at(0);
  if (primitive == "slice_size")
    return m_builder->CreateExtractValue(args.at(0), 1);
  if (primitive == "slice_ptr") {
    if (returns_mut) {
      llvm::Type* result_type = llvm_type(types[0]->without_qual());
      return m_builder->CreateStructGEP(result_type, args.at(0), 0, "sl.ptr.mut");
    }
    return m_builder->CreateExtractValue(args.at(0), 0, "sl.ptr.x");
  }
  if (primitive == "slice_dup") {
    return m_builder->CreateInsertValue(
        args.at(0), m_builder->CreateAdd(m_builder->CreateExtractValue(args.at(0), 1), args.at(1)), 1);
  }
  if (primitive == "set_at") {
    auto* result_type = llvm_type(*types[0]->without_qual().ptr_base());
    llvm::Value* value = args.at(2);
    llvm::Value* base = m_builder->CreateGEP(result_type, args.at(0), args.at(1), "p.set_at.gep");
    m_builder->CreateStore(value, base);
    return args.at(2);
  }
  if (primitive == "get_at") {
    auto* result_type = llvm_type(*types[0]->without_qual().ptr_base());
    llvm::Value* base = args.at(0);
    base = m_builder->CreateGEP(result_type, base, args.at(1), "p.get_at.gep");
    return base;
  }
  if (primitive.starts_with("ib_"))
    return int_bin_primitive(primitive, args);
  throw std::runtime_error("Unknown primitive "s + primitive);
};

template <> auto Compiler::expression(const ast::CallExpr& expr, bool mut) -> Val {
  auto* selected = expr.selected_overload();
  llvm::Function* llvm_fn = nullptr;
  const auto* ret_ty = selected->ast().val_ty();
  bool returns_mut = ret_ty != nullptr && ret_ty->is_mut();

  if (!returns_mut)
    not_mut("call returning by value", mut);

  vector<Val> args{};
  vector<const ty::Type*> arg_types{};
  vector<llvm::Value*> llvm_args{};

  unsigned j = 0;
  const auto& selected_args = selected->ast().args();
  for (const auto& i : expr.args()) {
    auto should_pass_by_mut = [&](unsigned index) {
      if (index >= selected_args.size())
        return false; // varargs function, logic will probably change later but for now, always pass by value
      return selected_args[index].val_ty()->is_mut();
    };

    auto arg = body_expression(i, should_pass_by_mut(j));
    args.push_back(arg);
    llvm_args.push_back(arg.llvm());
    arg_types.push_back(i.val_ty());
    j++;
  }

  Val val{nullptr};

  auto prim = primitive(selected, llvm_args, arg_types, ret_ty);
  if (prim.has_value()) {
    val = *prim;
  } else {
    llvm_fn = selected->declaration(*this);
    val = m_builder->CreateCall(llvm_fn, llvm_args);
  }

  if (returns_mut && !mut)
    return m_builder->CreateLoad(llvm_type(*ret_ty->qual_base()), val, "c.nmut.deref");
  return val;
}

template <> auto Compiler::expression(const ast::AssignExpr& expr, bool mut) -> Val {
  if (const auto* target_var = dyn_cast<ast::VarExpr>(&expr.target())) {
    auto expr_val = body_expression(expr.value(), mut);
    auto target_val = m_scope.at(target_var->name()).value;
    m_builder->CreateStore(expr_val, target_val);
    return expr_val;
  }
  if (const auto* field_access = dyn_cast<ast::FieldAccessExpr>(&expr.target())) {
    auto base = body_expression(field_access->base(), true);
    auto base_name = field_access->field();
    int base_offset = field_access->offset();

    auto expr_val = body_expression(expr.value(), mut);
    auto* struct_type = llvm_type(cast<ty::Struct>(field_access->base().val_ty()->without_qual()));

    auto* gep = m_builder->CreateStructGEP(struct_type, base, base_offset, "s.sf."s + base_name);
    m_builder->CreateStore(expr_val, gep);
    return expr_val;
  }
  throw std::runtime_error("Can't assign to target "s + expr.target().kind_name());
}

template <> auto Compiler::expression(const ast::CtorExpr& expr, bool mut) -> Val {
  const auto& type = *expr.val_ty();
  if (const auto* struct_type = dyn_cast<ty::Struct>(&type.without_qual())) {
    auto* llvm_struct_type = llvm_type(*struct_type);

    llvm::Value* alloc = nullptr;
    // TODO: #4 determine what kind of allocation must be done, and if at all. It'll probably require a complicated
    // semantic step to determine object lifetime, which would probably be evaluated before compilation of these
    // expressions. currently just using "mut" constraint, which probably won't be permanent and is probably
    // faulty, but, oh well

    //// Heap allocation
    if (mut) {
      auto* alloc_size = llvm::ConstantExpr::getSizeOf(llvm_struct_type);
      alloc = llvm::CallInst::CreateMalloc(m_builder->GetInsertBlock(), m_builder->getInt64Ty(), llvm_struct_type,
                                           alloc_size, nullptr, nullptr, "s.ctor.malloc");
      alloc = m_builder->Insert(alloc);
    }

    //// Stack allocation
    // alloc = m_builder->CreateAlloca(llvm_struct_type, 0, nullptr, "s.ctor.alloca");

    auto i = 0;
    llvm::Value* base_value = llvm::UndefValue::get(llvm_struct_type);
    for (const auto& arg : expr.args()) {
      const auto& [target_type, target_name] = struct_type->fields().begin()[i];
      auto field_value = body_expression(arg);
      base_value = m_builder->CreateInsertValue(base_value, field_value, i, "s.ctor.wf." + target_name);
      i++;
    }

    if (mut) {
      m_builder->CreateStore(base_value, alloc);
      base_value = alloc;
    }

    return base_value;
  }
  if (const auto* int_type = dyn_cast<ty::Int>(&type.without_qual())) {
    yume_assert(expr.args().size() == 1, "Numeric cast can only contain a single argument");
    auto& cast_from = expr.args()[0];
    yume_assert(isa<ty::Int>(cast_from.val_ty()->without_qual()), "Numeric cast must convert from int");
    auto base = body_expression(cast_from);
    if (cast<ty::Int>(cast_from.val_ty()->without_qual()).is_signed()) {
      return m_builder->CreateSExtOrTrunc(base, llvm_type(*int_type));
    }
    return m_builder->CreateZExtOrTrunc(base, llvm_type(*int_type));
  }
  if (const auto* slice_type = dyn_cast<ty::Ptr>(&type.without_qual());
      slice_type != nullptr && slice_type->has_qualifier(Qualifier::Slice)) {
    yume_assert(expr.args().size() == 1, "Slice constructor can only contain a single argument");
    auto& slice_size_expr = expr.args()[0];
    yume_assert(isa<ty::Int>(slice_size_expr.val_ty()->without_qual()), "Slice constructor must convert from int");
    auto slice_size = body_expression(slice_size_expr);

    auto* llvm_slice_type = llvm_type(*slice_type);
    const auto& base_ty_type = *slice_type->ptr_base(); // ???
    auto* base_type = llvm_type(base_ty_type);

    //// Stack allocation
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

    // TODO: the commented-out block above stack-allocates a slice constructor if its size can be determined
    // trivially. However, since it references stack memory, a slice allocated this way could never be feasibly
    // returned or passed into a function which stores a reference to it, etc. The compiler currently does nothing
    // resembling "escape analysis", however something like that might be needed to perform an optimization like
    // shown above.
    // TODO: Note that also a slice could be stack-allocated even if its size *wasn't* known at compile time,
    // however, I simply didn't know how to do that when i wrote the above snipped. But, since its problematic
    // anyway, it remains unfixed (and commented out); revisit later.
    // TODO: A large slice may be unfeasible to be stack-allocated anyway, so in addition to the above points,
    // slice size could also be a consideration. Perhaps we don't *want* to stack-allocate unknown-sized slices as
    // they may be absurdly huge in size and cause stack overflow.
    // Related: #4

    auto* alloc_size = llvm::ConstantExpr::getSizeOf(base_type);
    auto* array_size = m_builder->CreateSExt(slice_size, m_builder->getInt64Ty(), "sl.ctor.size");
    auto* array_alloc = llvm::CallInst::CreateMalloc(m_builder->GetInsertBlock(), m_builder->getInt64Ty(), base_type,
                                                     alloc_size, array_size, nullptr, "sl.ctor.malloc");

    auto* data_ptr = m_builder->Insert(array_alloc);
    auto* data_size = m_builder->CreateSExtOrBitCast(slice_size, m_builder->getInt64Ty());
    llvm::Value* slice_inst = llvm::UndefValue::get(llvm_slice_type);
    slice_inst = m_builder->CreateInsertValue(slice_inst, data_ptr, 0);
    slice_inst = m_builder->CreateInsertValue(slice_inst, data_size, 1);
    slice_inst->setName("sl.ctor.inst");

    auto* current_block = m_builder->GetInsertBlock();
    m_builder->SetInsertPoint(m_current_fn->m_decl_bb);
    auto* iter_alloc = m_builder->CreateAlloca(m_builder->getInt64Ty(), nullptr, "sl.ctor.definit.iter");
    m_builder->SetInsertPoint(current_block);
    m_builder->CreateStore(m_builder->getInt64(0), iter_alloc);

    auto* iter_test = llvm::BasicBlock::Create(*m_context, "sl.ctor.definit.test", *m_current_fn);
    auto* iter_body = llvm::BasicBlock::Create(*m_context, "sl.ctor.definit.head", *m_current_fn);
    auto* iter_merge = llvm::BasicBlock::Create(*m_context, "sl.ctor.definit.merge", *m_current_fn);

    m_builder->CreateBr(iter_test);
    m_builder->SetInsertPoint(iter_test);

    auto* iter_less = m_builder->CreateICmpSLT(m_builder->CreateLoad(m_builder->getInt64Ty(), iter_alloc), data_size,
                                               "sl.ctor.definit.cmp");
    m_builder->CreateCondBr(iter_less, iter_body, iter_merge);

    m_builder->SetInsertPoint(iter_body);
    auto* iter_next_addr = m_builder->CreateInBoundsGEP(
        base_type, data_ptr, m_builder->CreateLoad(m_builder->getInt64Ty(), iter_alloc), "sl.ctor.definit.gep");
    m_builder->CreateStore(default_init(base_ty_type), iter_next_addr);
    m_builder->CreateStore(
        m_builder->CreateAdd(m_builder->CreateLoad(m_builder->getInt64Ty(), iter_alloc), m_builder->getInt64(1)),
        iter_alloc);
    m_builder->CreateBr(iter_test);

    m_builder->SetInsertPoint(iter_merge);

    return slice_inst;
  }

  throw std::runtime_error("Can't construct non-struct, non-integer, non-slice type");
}

template <> auto Compiler::expression(const ast::SliceExpr& expr, bool mut) -> Val {
  not_mut("slice literal", mut);

  auto* slice_size = m_builder->getInt64(expr.args().size());

  auto* slice_type = llvm_type(*expr.val_ty());
  auto* base_type = llvm_type(*expr.val_ty()->ptr_base()); // ???

  auto* alloc_size = llvm::ConstantExpr::getSizeOf(base_type);
  auto* ptr_alloc = llvm::CallInst::CreateMalloc(m_builder->GetInsertBlock(), m_builder->getInt64Ty(), base_type,
                                                 alloc_size, slice_size, nullptr, "sl.ctor.malloc");
  auto* data_ptr = m_builder->Insert(ptr_alloc);

  unsigned j = 0;
  for (const auto& i : expr.args())
    m_builder->CreateStore(body_expression(i), m_builder->CreateConstInBoundsGEP1_32(base_type, data_ptr, j++));

  llvm::Value* slice_inst = llvm::UndefValue::get(slice_type);
  slice_inst = m_builder->CreateInsertValue(slice_inst, data_ptr, 0);
  slice_inst = m_builder->CreateInsertValue(slice_inst, slice_size, 1);

  return slice_inst;
}

template <> auto Compiler::expression(const ast::FieldAccessExpr& expr, bool mut) -> Val {
  // TODO: struct can only contain things by value, later this needs a condition
  not_mut("immutable field", mut);

  auto base = body_expression(expr.base());
  auto base_name = expr.field();
  int base_offset = expr.offset();

  return m_builder->CreateExtractValue(base, base_offset, "s.field."s + base_name);
}

template <> auto Compiler::expression(const ast::ImplicitCastExpr& expr, bool mut) -> Val {
  const auto* target_ty = expr.val_ty();
  const auto* current_ty = expr.base().val_ty();
  llvm::Value* base = body_expression(expr.base(), current_ty->is_mut());

  if (mut != target_ty->is_mut()) {
    throw std::logic_error("Implicit cast target and expression mutability don't match!");
  }

  if (expr.conversion().dereference) {
    yume_assert(current_ty->is_mut(), "Source type must be mutable when implicitly derefencing");
    current_ty = current_ty->qual_base();
    base = m_builder->CreateLoad(llvm_type(*current_ty), base, "ic.deref");
  }

  if (expr.conversion().kind == ty::Conv::Int) {
    return m_builder->CreateIntCast(base, llvm_type(*target_ty), cast<ty::Int>(current_ty)->is_signed(), "ic.int");
  }

  return base;
}

void Compiler::write_object(const char* filename, bool binary) {
  auto dest = open_file(filename);

  llvm::legacy::PassManager pass;
  auto fileType = binary ? llvm::CGFT_ObjectFile : llvm::CGFT_AssemblyFile;

  if (m_targetMachine->addPassesToEmitFile(pass, *dest, nullptr, fileType)) {
    llvm::errs() << "TargetMachine can't emit a file of this type";
    throw std::exception();
  }

  pass.run(*m_module);
  dest->flush();
}

auto Compiler::mangle_name(const Fn& fn) -> string {
  stringstream ss{};
  ss << "_Ym.";
  ss << fn.ast().name();
  ss << "(";
  int idx = 0;
  for (const auto& i : fn.ast().args()) {
    if (idx++ > 0)
      ss << ",";
    ss << mangle_name(*i.type().val_ty(), fn);
  }
  ss << ")";
  // TODO: should mangled names even contain the return type...?
  if (fn.ast().ret().has_value())
    ss << mangle_name(*fn.ast().ret()->get().val_ty(), fn); // wtf

  return ss.str();
}

auto Compiler::mangle_name(const ty::Type& ast_type, const Fn& parent) -> string {
  stringstream ss{};
  if (const auto* qual_type = dyn_cast<ty::Qual>(&ast_type)) {
    ss << mangle_name(qual_type->base(), parent);
    if (qual_type->has_qualifier(Qualifier::Mut))
      ss << "&";
    return ss.str();
  }
  if (const auto* ptr_type = dyn_cast<ty::Ptr>(&ast_type)) {
    ss << mangle_name(ptr_type->base(), parent);
    if (ptr_type->has_qualifier(Qualifier::Ptr))
      ss << "*";
    if (ptr_type->has_qualifier(Qualifier::Slice))
      ss << "[";
    return ss.str();
  }
  if (const auto* generic_type = dyn_cast<ty::Generic>(&ast_type)) {
    auto match = parent.m_subs.find(generic_type->name());
    yume_assert(match != parent.m_subs.end(), "Cannot mangle unsubstituted generic");
    return match->second->name();
  }
  return ast_type.name();
}

auto Compiler::known_type(const string& str) -> ty::Type& { return *m_types.known.find(str)->getValue(); }

void Compiler::body_statement(const ast::Stmt& stat) {
  const ASTStackTrace guard("Codegen: "s + stat.kind_name() + " statement", stat);
  return CRTPWalker::body_statement(stat);
}

// TODO: #3 once implicit conversion checking is added in the remaining locations (i.e. field assignment and return)
// there is no real reason to keep the `mut` parameter on all expression compilation methods. All the mutability
// information is in the type system and dereferences are handled by the TypeWalker.
auto Compiler::body_expression(const ast::Expr& expr, bool mut) -> Val {
  const ASTStackTrace guard("Codegen: "s + expr.kind_name() + " expression", expr);
  return CRTPWalker::body_expression(expr, mut);
}
} // namespace yume
