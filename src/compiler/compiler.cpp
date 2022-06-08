//
// Created by rymiel on 5/8/22.
//

#include "compiler.hpp"
#include "../ast.hpp"
#include "../type.hpp"
#include "../util.hpp"
#include "type_walker.hpp"
#include "vals.hpp"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
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
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace yume {
Compiler::Compiler(std::vector<SourceFile> source_files)
    : m_sources(std::move(source_files)), m_walker(std::make_unique<TypeWalker>(*this)) {
  m_context = std::make_unique<LLVMContext>();
  m_module = std::make_unique<Module>("yume", *m_context);
  m_builder = std::make_unique<IRBuilder<>>(*m_context);

  InitializeNativeTarget();
  InitializeNativeTargetAsmParser();
  InitializeNativeTargetAsmPrinter();
  string error;
  string targetTriple = sys::getDefaultTargetTriple();
  const auto* target = TargetRegistry::lookupTarget(targetTriple, error);

  if (target == nullptr) {
    errs() << error;
    throw std::exception();
  }
  const char* cpu = "generic";
  const char* feat = "";

  TargetOptions opt;
  m_targetMachine =
      unique_ptr<TargetMachine>(target->createTargetMachine(targetTriple, cpu, feat, opt, Reloc::Model::PIC_));

  m_module->setDataLayout(m_targetMachine->createDataLayout());
  m_module->setTargetTriple(targetTriple);
}

void Compiler::run() {
  for (const auto& source : m_sources) {
    for (auto& i : source.m_program->body()) {
      decl_statement(i, nullptr, source.m_program.get());
    }
  }

  walk_types();

  for (auto& fn : m_fns) {
    if (fn.name() == "main") {
      fn.m_llvm_fn = declare(fn, false);
    }
  }

  while (!m_decl_queue.empty()) {
    auto* next = m_decl_queue.front();
    m_decl_queue.pop();
    define(*next);
  }
}

void Compiler::walk_types() {
  // First pass: only convert function parameters
  for (auto& fn : m_fns) {
    m_walker->m_current_fn = &fn;
    m_walker->body_statement(fn.ast());
  }

  // Second pass: convert everything else, but only when instantiated
  m_walker->m_in_depth = true;
}

void Compiler::decl_statement(ast::Stmt& stmt, ty::Type* parent, ast::Program* member) {
  if (auto* fn_decl = dyn_cast<ast::FnDecl>(&stmt)) {
    vector<unique_ptr<ty::Generic>> type_args{};
    std::map<string, const ty::Type*> subs{};
    for (auto& i : fn_decl->type_args()) {
      auto& gen = type_args.emplace_back(std::make_unique<ty::Generic>(i));
      subs.try_emplace(i, gen.get());
    }
    m_fns.emplace_back(*fn_decl, parent, member, std::move(subs), std::move(type_args));
  } else if (auto* s_decl = dyn_cast<ast::StructDecl>(&stmt)) {
    auto fields = vector<const ast::TypeName*>();
    fields.reserve(s_decl->fields().size());
    for (const auto& f : s_decl->fields()) {
      fields.push_back(&f);
    };
    auto i_ty = m_types.known.insert({s_decl->name(), std::make_unique<ty::Struct>(s_decl->name(), fields)});

    for (auto& f : s_decl->body().body()) {
      decl_statement(f, i_ty.first->second.get());
    }
  }
}

auto Compiler::convert_type(const ast::Type& ast_type, const ty::Type* parent, Fn* context) -> const ty::Type& {
  if (const auto* simple_type = dyn_cast<ast::SimpleType>(&ast_type)) {
    auto name = simple_type->name();
    if (context != nullptr) {
      auto generic = context->m_subs.find(name);
      if (generic != context->m_subs.end()) {
        return *generic->second;
      }
    }
    auto val = m_types.known.find(name);
    if (val != m_types.known.end()) {
      return *val->second;
    }
  } else if (const auto* qual_type = dyn_cast<ast::QualType>(&ast_type)) {
    auto qualifier = qual_type->qualifier();
    return convert_type(qual_type->base(), parent, context).known_qual(qualifier);
  } else if (isa<ast::SelfType>(ast_type)) {
    if (parent != nullptr) {
      return *parent;
    }
  }

  return m_types.unknown;
}

auto Compiler::llvm_type(const ty::Type& type) -> llvm::Type* {
  if (const auto* int_type = dyn_cast<ty::Int>(&type)) {
    return llvm::Type::getIntNTy(*m_context, int_type->size());
  }
  if (const auto* qual_type = dyn_cast<ty::Qual>(&type)) {
    return llvm_type(qual_type->base())->getPointerTo();
  }
  if (const auto* ptr_type = dyn_cast<ty::Ptr>(&type)) {
    switch (ptr_type->qualifier()) {
    case Qualifier::Ptr: return llvm::PointerType::getUnqual(llvm_type(ptr_type->base()));
    case Qualifier::Slice: {
      auto args = vector<llvm::Type*>{};
      args.push_back(llvm::PointerType::getUnqual(llvm_type(ptr_type->base())));
      args.push_back(llvm::Type::getInt64Ty(*m_context));
      return llvm::StructType::get(*m_context, args);
    }
    default: return llvm_type(ptr_type->base());
    }
  }
  if (const auto* struct_type = dyn_cast<ty::Struct>(&type)) {
    auto* memo = struct_type->memo();
    if (memo == nullptr) {
      auto fields = vector<llvm::Type*>{};
      for (const auto& i : struct_type->fields()) {
        fields.push_back(llvm_type(convert_type(i.type())));
      }
      memo = llvm::StructType::create(*m_context, fields, "_"s + struct_type->name());
      struct_type->memo(memo);
    }

    return memo;
  }

  return Type::getVoidTy(*m_context);
}

auto Compiler::declare(Fn& fn, bool mangle) -> llvm::Function* {
  if (fn.m_llvm_fn != nullptr) {
    return fn.m_llvm_fn;
  }
  const auto& fn_decl = fn.m_ast_decl;
  auto* llvm_ret_type = llvm::Type::getVoidTy(*m_context);
  auto llvm_args = vector<llvm::Type*>{};
  if (fn_decl.ret()) {
    llvm_ret_type = llvm_type(*fn_decl.ret()->get().val_ty());
  }
  for (const auto& i : fn_decl.args()) {
    llvm_args.push_back(llvm_type(*i.val_ty()));
  }
  llvm::FunctionType* fn_t = llvm::FunctionType::get(llvm_ret_type, llvm_args, fn_decl.varargs());

  string name = fn_decl.name();
  if (mangle) {
    name = mangle_name(fn);
  }

  auto linkage = mangle ? Function::InternalLinkage : Function::ExternalLinkage;
  Function* llvm_fn = Function::Create(fn_t, linkage, name, m_module.get());

  int arg_i = 0;
  for (auto& arg : llvm_fn->args()) {
    arg.setName("arg."s + fn_decl.args()[arg_i].name());
    arg_i++;
  }

  if (!fn_decl.primitive()) { // Skip primitive definitions
    m_decl_queue.push(&fn);
    m_walker->m_current_fn = &fn;
    m_walker->body_statement(fn.ast());
  }
  return llvm_fn;
}

template <> void Compiler::statement(const ast::Compound& stat) {
  for (const auto& i : stat.body()) {
    body_statement(i);
  }
}

void Compiler::define(Fn& fn) {
  m_current_fn = &fn;
  m_scope.clear();
  BasicBlock* bb = BasicBlock::Create(*m_context, "entry", fn);
  m_builder->SetInsertPoint(bb);

  int i = 0;
  for (auto& arg : fn.llvm()->args()) {
    const auto& type = *fn.ast().args()[i].val_ty();
    auto name = fn.ast().args()[i++].name();
    llvm::Value* alloc = nullptr;
    if (const auto* qual_type = dyn_cast<ty::Qual>(&type)) {
      if (qual_type->is_mut()) {
        alloc = &arg;
        m_scope.insert({name, &arg});
        continue;
      }
    }
    alloc = m_builder->CreateAlloca(llvm_type(type), nullptr, name);
    m_builder->CreateStore(&arg, alloc);
    m_scope.insert({name, alloc});
  }

  if (const auto* body = get_if<unique_ptr<ast::Compound>>(&fn.body()); body != nullptr) {
    statement(**body);
  }
  if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
    m_builder->CreateRetVoid();
  }
  verifyFunction(*fn, &llvm::errs());
}

template <> void Compiler::statement(const ast::WhileStmt& stat) {
  auto* test_bb = BasicBlock::Create(*m_context, "while.test", *m_current_fn);
  auto* head_bb = BasicBlock::Create(*m_context, "while.head", *m_current_fn);
  auto* merge_bb = BasicBlock::Create(*m_context, "while.merge", *m_current_fn);
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
  auto* merge_bb = BasicBlock::Create(*m_context, "if.cont", *m_current_fn);
  auto* next_test_bb = BasicBlock::Create(*m_context, "if.test", *m_current_fn, merge_bb);
  bool all_terminated = true;
  m_builder->CreateBr(next_test_bb);

  auto clauses = stat.clauses();
  for (auto b = clauses.begin(); b != clauses.end(); ++b) {
    m_builder->SetInsertPoint(next_test_bb);
    auto* body_bb = BasicBlock::Create(*m_context, "if.then", *m_current_fn, merge_bb);
    next_test_bb = BasicBlock::Create(*m_context, "if.test", *m_current_fn, merge_bb);
    auto condition = body_expression(b->cond());
    m_builder->CreateCondBr(condition, body_bb, next_test_bb);
    m_builder->SetInsertPoint(body_bb);
    statement(b->body());
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      all_terminated = false;
      m_builder->CreateBr(merge_bb);
    }
  }

  if (stat.else_clause().has_value()) {
    next_test_bb->setName("if.else");
    m_builder->SetInsertPoint(next_test_bb);
    statement(stat.else_clause()->get());
    if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
      all_terminated = false;
      m_builder->CreateBr(merge_bb);
    }
  }

  if (all_terminated) {
    merge_bb->eraseFromParent();
  } else {
    m_builder->SetInsertPoint(merge_bb);
  }
}

template <> void Compiler::statement(const ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    auto val = body_expression(stat.expr().value());
    m_builder->CreateRet(val);
    return;
  }
  m_builder->CreateRetVoid();
}

template <> void Compiler::statement(const ast::VarDecl& stat) {
  // Locals are currently always mut, get the base type instead
  // TODO: revisit, probably extract logic
  auto* var_type = llvm_type(*stat.val_ty()->qual_base());

  auto* alloc = m_builder->CreateAlloca(var_type, nullptr, stat.name());
  auto expr_val = body_expression(stat.init());
  m_builder->CreateStore(expr_val, alloc);
  m_scope.insert({stat.name(), alloc});
}

static inline void not_mut(const string& message, bool mut) {
  if (mut) {
    throw std::runtime_error(message + " cannot be mutable!");
  }
}

template <> auto Compiler::expression(const ast::NumberExpr& expr, bool mut) -> Val {
  not_mut("number constant", mut);
  auto val = expr.val();
  if (expr.val_ty() == m_types.int64().s_ty) {
    return m_builder->getInt64(val);
  }
  return m_builder->getInt32(val);
}

template <> auto Compiler::expression(const ast::CharExpr& expr, bool mut) -> Val {
  not_mut("character constant", mut);
  return m_builder->getInt8(expr.val());
}

template <> auto Compiler::expression(const ast::StringExpr& expr, bool mut) -> Val {
  not_mut("string constant", mut);
  auto val = expr.val();

  std::vector<llvm::Constant*> chars(val.length());
  for (unsigned int i = 0; i < val.size(); i++) {
    chars[i] = m_builder->getInt8(val[i]);
  }

  chars.push_back(m_builder->getInt8(0));
  auto* stringType = llvm::ArrayType::get(m_builder->getInt8Ty(), chars.size());
  auto* init = llvm::ConstantArray::get(stringType, chars);
  auto* global = new llvm::GlobalVariable(*m_module, stringType, true, GlobalVariable::PrivateLinkage, init, ".str");
  return ConstantExpr::getBitCast(global, m_builder->getInt8PtrTy(0));
}

template <> auto Compiler::expression(const ast::VarExpr& expr, bool mut) -> Val {
  auto* val = m_scope.at(expr.name()).llvm();
  if (!mut) {
    // Function arguments can act as locals, but they can be immutable, but still behind a reference (alloca)
    val = m_builder->CreateLoad(llvm_type(expr.val_ty()->without_qual()), val);
  }
  return val;
}

template <> auto Compiler::expression(const ast::CallExpr& expr, bool mut) -> Val {
  // TODO: calls can only return by value right now, but later this needs a condition
  not_mut("call returning by value", mut);

  auto* selected = expr.selected_overload();
  llvm::Function* llvm_fn = nullptr;

  vector<Val> args{};
  vector<llvm::Value*> llvm_args{};
  unsigned j = 0;
  auto selected_args = selected->ast().args();
  for (const auto& i : expr.args()) {
    auto should_pass_by_mut = [&](unsigned index) {
      if (index >= selected_args.size()) {
        return false; // varargs function, logic will probably change later but for now, always pass by value
      }
      return selected_args[index].val_ty()->is_mut();
    };

    auto arg = body_expression(i, should_pass_by_mut(j++));
    args.push_back(arg);
    llvm_args.push_back(arg.llvm());
  }

  if (selected->ast().primitive()) {
    auto primitive = get<string>(selected->body());
    if (primitive == "libc") {
      llvm_fn = selected->declaration(*this, false);
    } else if (primitive == "ptrto") {
      return args.at(0);
    } else if (primitive == "icmp_sgt") {
      return m_builder->CreateICmpSGT(args.at(0), args.at(1));
    } else if (primitive == "icmp_ugt") {
      return m_builder->CreateICmpUGT(args.at(0), args.at(1));
    } else if (primitive == "icmp_slt") {
      return m_builder->CreateICmpSLT(args.at(0), args.at(1));
    } else if (primitive == "icmp_ult") {
      return m_builder->CreateICmpULT(args.at(0), args.at(1));
    } else if (primitive == "icmp_eq") {
      return m_builder->CreateICmpEQ(args.at(0), args.at(1));
    } else if (primitive == "icmp_ne") {
      return m_builder->CreateICmpNE(args.at(0), args.at(1));
    } else if (primitive == "add") {
      return m_builder->CreateAdd(args.at(0), args.at(1));
    } else if (primitive == "mul") {
      return m_builder->CreateMul(args.at(0), args.at(1));
    } else if (primitive == "srem") {
      return m_builder->CreateSRem(args.at(0), args.at(1));
    } else if (primitive == "urem") {
      return m_builder->CreateURem(args.at(0), args.at(1));
    } else if (primitive == "sdiv") {
      return m_builder->CreateSDiv(args.at(0), args.at(1));
    } else if (primitive == "udiv") {
      return m_builder->CreateUDiv(args.at(0), args.at(1));
    } else {
      throw std::runtime_error("Unknown primitive "s + primitive);
    }
  } else {
    llvm_fn = selected->declaration(*this);
  }

  return m_builder->CreateCall(llvm_fn, llvm_args);
}

template <> auto Compiler::expression(const ast::AssignExpr& expr, bool mut) -> Val {
  if (const auto* target_var = dyn_cast<ast::VarExpr>(&expr.target())) {
    auto expr_val = body_expression(expr.value(), mut);
    auto target_val = m_scope.at(target_var->name());
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
    // TODO: determine what kind of allocation must be done, and if at all. It'll probably require a complicated
    // semantic step to determine object lifetime, which would probably be evaluated before compilation of these
    // expressions. currently just using "mut" constraint, which probably won't be permanent and is probably faulty,
    // but, oh well

    //// Heap allocation
    if (mut) {
      auto* alloc_size = ConstantExpr::getSizeOf(llvm_struct_type);
      alloc_size = ConstantExpr::getTruncOrBitCast(alloc_size, m_builder->getInt32Ty());
      alloc = llvm::CallInst::CreateMalloc(m_builder->GetInsertBlock(), m_builder->getInt32Ty(), llvm_struct_type,
                                           alloc_size, nullptr, nullptr, "s.ctor.malloc");
      alloc = m_builder->Insert(alloc);
    }

    //// Stack allocation
    // alloc = m_builder->CreateAlloca(llvm_struct_type, 0, nullptr, "s.ctor.alloca");

    auto i = 0;
    llvm::Value* base_value = UndefValue::get(llvm_struct_type);
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

  throw std::runtime_error("Can't construct non-struct type");
}

template <> auto Compiler::expression(const ast::SliceExpr& expr, bool mut) -> Val {
  not_mut("slice literal", mut);
  auto values = vector<Val>();
  auto slice_size = expr.args().size();
  values.reserve(slice_size);
  for (const auto& i : expr.args()) {
    values.push_back(body_expression(i));
  }

  auto* slice_type = llvm_type(*expr.val_ty()->qual_base());
  auto* base_type = llvm_type(*expr.val_ty()->qual_base()->ptr_base()); // ???
  auto* array_type = ArrayType::get(base_type, slice_size);
  auto* array_alloc = m_builder->CreateAlloca(array_type);
  unsigned j = 0;
  for (const auto& i : values) {
    m_builder->CreateStore(i, m_builder->CreateConstInBoundsGEP2_32(array_type, array_alloc, 0, j++));
  }
  auto* data_ptr = m_builder->CreateBitCast(m_builder->CreateLoad(array_type, array_alloc), base_type->getPointerTo());
  llvm::Value* slice_alloc = m_builder->CreateAlloca(slice_type);
  slice_alloc = m_builder->CreateInsertValue(slice_alloc, data_ptr, 0);
  slice_alloc = m_builder->CreateInsertValue(slice_alloc, m_builder->getInt64(slice_size), 1);

  return slice_alloc;
}

template <> auto Compiler::expression(const ast::FieldAccessExpr& expr, bool mut) -> Val {
  // TODO: struct can only contain things by value, later this needs a condition
  not_mut("immutable field", mut);

  auto base = body_expression(expr.base());
  auto base_name = expr.field();
  int base_offset = expr.offset();

  return m_builder->CreateExtractValue(base, base_offset, "s.field."s + base_name);
}

void Compiler::write_object(const char* filename, bool binary) {
  auto dest = open_file(filename);

  legacy::PassManager pass;
  auto fileType = binary ? CGFT_ObjectFile : CGFT_AssemblyFile;

  if (m_targetMachine->addPassesToEmitFile(pass, *dest, nullptr, fileType)) {
    errs() << "TargetMachine can't emit a file of this type";
    throw std::exception();
  }

  pass.run(*m_module);
  dest->flush();
}

auto Compiler::mangle_name(const Fn& fn) -> string {
  std::stringstream ss{};
  ss << "_Ym.";
  ss << fn.ast().name();
  ss << "(";
  int idx = 0;
  for (const auto& i : fn.ast().args()) {
    if (idx++ > 0) {
      ss << ",";
    }
    ss << mangle_name(i.type(), fn);
  }
  ss << ")";
  // TODO: should mangled names even contain the return type...?
  if (fn.ast().ret().has_value()) {
    ss << mangle_name(fn.ast().ret().value(), fn);
  }
  return ss.str();
}

auto Compiler::mangle_name(const ast::Type& ast_type, const Fn& parent) -> string {
  if (const auto* simple_type = dyn_cast<ast::SimpleType>(&ast_type)) {
    auto name = simple_type->name();
    if (auto match = parent.m_subs.find(name); match != parent.m_subs.end()) {
      return match->second->name();
    }
    return simple_type->name();
  }
  if (isa<ast::SelfType>(ast_type)) {
    return parent.parent()->name();
  }
  std::stringstream ss{};
  const auto& qual_type = cast<ast::QualType>(ast_type);
  auto qualifier = qual_type.qualifier();
  ss << mangle_name(qual_type.base(), parent);
  switch (qualifier) {
  case Qualifier::Ptr: ss << "*"; break;
  case Qualifier::Slice: ss << "["; break;
  case Qualifier::Mut: ss << "&"; break;
  case Qualifier::Scope: ss << "@"; break;
  default: assert("Should never happen"); // NOLINT
  }
  return ss.str();
}

auto Compiler::known_type(const string& str) -> ty::Type& { return *m_types.known.find(str)->getValue(); }

void Compiler::body_statement(const ast::Stmt& stat) { return CRTPWalker::body_statement(stat); };
auto Compiler::body_expression(const ast::Expr& expr, bool mut) -> Val {
  return CRTPWalker::body_expression(expr, mut);
};
} // namespace yume
