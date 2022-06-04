//
// Created by rymiel on 5/8/22.
//

#include "compiler.hpp"
#include "ast.hpp"
#include "type.hpp"
#include <bits/ranges_algo.h>
#include <climits>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace yume {

TypeHolder::TypeHolder() {
  int j = 0;
  for (auto i : {8, 16, 32, 64}) {
    auto i_signed = std::make_unique<ty::IntegerType>(i, true);
    auto i_unsigned = std::make_unique<ty::IntegerType>(i, false);
    int_types.at(j++) = {i_signed.get(), i_unsigned.get()};
    known.insert({"I"s + std::to_string(i), move(i_signed)});
    known.insert({"U"s + std::to_string(i), move(i_unsigned)});
  }
}

Compiler::Compiler(std::vector<SourceFile> source_files) : m_sources(std::move(source_files)) {
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
  auto relocModel = Optional<Reloc::Model>();
  m_targetMachine = unique_ptr<TargetMachine>(target->createTargetMachine(targetTriple, cpu, feat, opt, relocModel));

  m_module->setDataLayout(m_targetMachine->createDataLayout());
  m_module->setTargetTriple(targetTriple);

  for (const auto& source : m_sources) {
    for (const auto& i : source.m_program->body()) {
      if (i.kind() == ast::FnDeclKind) {
        const auto& fn_decl = dynamic_cast<const ast::FnDecl&>(i);
        auto& fn = m_fns.emplace_back(fn_decl);
        if (fn_decl.name() == "main") {
          fn.m_llvm_fn = declare(fn, false);
        }
      } else if (i.kind() == ast::StructDeclKind) {
        const auto& struct_decl = dynamic_cast<const ast::StructDecl&>(i);
        auto fields = vector<const ast::TypeName*>();
        fields.reserve(struct_decl.fields().size());
        for (const auto& f : struct_decl.fields()) {
          fields.push_back(&f);
        };
        auto struct_ty = std::make_unique<ty::StructType>(struct_decl.name(), fields);
        m_types.known.insert({struct_decl.name(), move(struct_ty)});
      }
    }
  }

  while (!m_decl_queue.empty()) {
    auto* next = m_decl_queue.front();
    m_decl_queue.pop();
    define(*next);
  }
}

auto Compiler::convert_type(const ast::Type& ast_type) -> ty::Type& {
  if (ast_type.kind() == ast::SimpleTypeKind) {
    const auto& simple_type = dynamic_cast<const ast::SimpleType&>(ast_type);
    auto name = simple_type.name();
    auto val = m_types.known.find(name);
    if (val != m_types.known.end()) {
      return *val->second;
    }
  } else {
    const auto& qual_type = dynamic_cast<const ast::QualType&>(ast_type);
    auto qualifier = qual_type.qualifier();
    return convert_type(qual_type.base()).known_qual(qualifier);
  }

  return m_types.unknown;
}

auto Compiler::llvm_type(const ty::Type& type) -> llvm::Type* {
  if (type.kind() == ty::Kind::Integer) {
    const auto& int_type = dynamic_cast<const ty::IntegerType&>(type);
    return llvm::Type::getIntNTy(*m_context, int_type.size());
  }
  if (type.kind() == ty::Kind::Qual) {
    const auto& qual_type = dynamic_cast<const ty::QualType&>(type);
    auto qualifier = qual_type.qualifier();
    switch (qualifier) {
    case ast::QualType::Qualifier::Ptr: return llvm::PointerType::getUnqual(llvm_type(qual_type.base()));
    case ast::QualType::Qualifier::Slice: {
      auto args = vector<llvm::Type*>{};
      args.push_back(llvm::PointerType::getUnqual(llvm_type(qual_type.base())));
      args.push_back(llvm::Type::getInt64PtrTy(*m_context));
      return llvm::StructType::get(*m_context, args);
    }
    default: return llvm_type(qual_type.base());
    }
  }
  if (type.kind() == ty::Kind::Struct) {
    const auto& struct_type = dynamic_cast<const ty::StructType&>(type);
    auto* memo = struct_type.memo();
    if (memo == nullptr) {
      auto fields = vector<llvm::Type*>{};
      for (const auto& i : struct_type.fields()) {
        fields.push_back(llvm_type(convert_type(i.type())));
      }
      memo = llvm::StructType::create(*m_context, fields, "_"s + struct_type.name());
      struct_type.memo(memo);
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
  auto* ret_type = llvm::Type::getVoidTy(*m_context);
  auto args = vector<llvm::Type*>{};
  if (fn_decl.ret()) {
    ret_type = llvm_type(convert_type(fn_decl.ret().value()));
  }
  for (const auto& i : fn_decl.args()) {
    args.push_back(llvm_type(convert_type(i.type())));
  }
  llvm::FunctionType* fn_t = llvm::FunctionType::get(ret_type, args, fn_decl.varargs());

  string name = fn_decl.name();
  if (mangle) {
    name = mangle_name(fn_decl);
  }

  auto linkage = mangle ? Function::InternalLinkage : Function::ExternalLinkage;
  Function* llvm_fn = Function::Create(fn_t, linkage, name, m_module.get());

  int arg_i = 0;
  for (auto& arg : llvm_fn->args()) {
    arg.setName("arg."s + fn_decl.args().begin()[arg_i].name());
    arg_i++;
  }

  if (!fn_decl.primitive()) { // Skip primitive definitions
    m_decl_queue.push(&fn);
  }
  return llvm_fn;
}

void Compiler::define(Fn& fn) {
  m_current_fn = &fn;
  m_scope.clear();
  BasicBlock* bb = BasicBlock::Create(*m_context, "entry", fn);
  m_builder->SetInsertPoint(bb);

  int i = 0;
  for (auto& arg : fn.llvm()->args()) {
    const auto& [type, name] = fn.m_ast_decl.args().begin()[i++];
    auto& yume_type = convert_type(type);
    auto* alloc = m_builder->CreateAlloca(llvm_type(yume_type), nullptr, name);
    m_builder->CreateStore(&arg, alloc);
    m_scope.insert({name, {alloc, &yume_type}});
  }

  if (const auto* body = get_if<unique_ptr<ast::Compound>>(&fn.body()); body != nullptr) {
    statement(**body);
  }
  if (m_builder->GetInsertBlock()->getTerminator() == nullptr) {
    m_builder->CreateRetVoid();
  }
  verifyFunction(*fn, &llvm::errs());
}

void Compiler::statement(const ast::Compound& stat) {
  for (const auto& i : stat.body()) {
    body_statement(i);
  }
}

void Compiler::statement(const ast::WhileStmt& stat) {
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

void Compiler::statement(const ast::IfStmt& stat) {
  auto* merge_bb = BasicBlock::Create(*m_context, "if.cont", *m_current_fn);
  auto* next_test_bb = BasicBlock::Create(*m_context, "if.test", *m_current_fn, merge_bb);
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
      m_builder->CreateBr(merge_bb);
    }
  }

  if (stat.else_clause().has_value()) {
    next_test_bb->setName("if.else");
    m_builder->SetInsertPoint(next_test_bb);
    statement(stat.else_clause()->get());
    m_builder->CreateBr(merge_bb);
  }
  m_builder->SetInsertPoint(merge_bb);
}

void Compiler::statement(const ast::ReturnStmt& stat) {
  if (stat.expr().has_value()) {
    auto val = body_expression(stat.expr().value());
    m_builder->CreateRet(val);
    return;
  }
  m_builder->CreateRetVoid();
}

void Compiler::statement(const ast::VarDecl& stat) {
  llvm::Type* var_type = nullptr;
  if (stat.type().has_value()) {
    var_type = llvm_type(convert_type(stat.type().value()));
  }

  auto* alloc = m_builder->CreateAlloca(var_type, nullptr, stat.name());
  auto expr_val = body_expression(stat.init());
  m_builder->CreateStore(expr_val, alloc);
  m_scope.insert({stat.name(), {alloc, expr_val.type()}});
}

void Compiler::body_statement(const ast::Stmt& stat) {
  auto kind = stat.kind();
  switch (kind) {
  case ast::CompoundKind: return statement(dynamic_cast<const ast::Compound&>(stat));
  case ast::WhileKind: return statement(dynamic_cast<const ast::WhileStmt&>(stat));
  case ast::IfKind: return statement(dynamic_cast<const ast::IfStmt&>(stat));
  case ast::ReturnKind: return statement(dynamic_cast<const ast::ReturnStmt&>(stat));
  case ast::VarDeclKind: return statement(dynamic_cast<const ast::VarDecl&>(stat));
  default: body_expression(dynamic_cast<const ast::Expr&>(stat));
  }
}

auto Compiler::expression(const ast::NumberExpr& expr, bool mut) -> Val {
  auto val = expr.val();
  if (val > std::numeric_limits<int32_t>::max()) {
    return {m_builder->getInt64(val), m_types.int64().signed_ty};
  }
  return {m_builder->getInt32(val), m_types.int32().signed_ty};
}

auto Compiler::expression(const ast::CharExpr& expr, bool mut) -> Val {
  return {m_builder->getInt8(expr.val()), m_types.int8().unsigned_ty};
}

auto Compiler::expression(const ast::StringExpr& expr, bool mut) -> Val {
  auto val = expr.val();

  std::vector<llvm::Constant*> chars(val.length());
  for (unsigned int i = 0; i < val.size(); i++) {
    chars[i] = m_builder->getInt8(val[i]);
  }

  chars.push_back(m_builder->getInt8(0));
  auto* stringType = llvm::ArrayType::get(m_builder->getInt8Ty(), chars.size());
  auto* init = llvm::ConstantArray::get(stringType, chars);
  auto* global = new llvm::GlobalVariable(*m_module, stringType, true, GlobalVariable::PrivateLinkage, init, ".str");
  return {ConstantExpr::getBitCast(global, m_builder->getInt8PtrTy(0)), &m_types.int8().unsigned_ty->known_ptr()};
}

auto Compiler::expression(const ast::VarExpr& expr, bool mut) -> Val {
  auto local = m_scope.at(expr.name());
  auto* val = local.llvm();
  if (!mut) {
    val = m_builder->CreateLoad(val->getType()->getPointerElementType(), val);
  }
  return {val, local.type()};
}

auto is_signed_type(ty::Type* type) -> bool {
  if (type == nullptr) {
    throw std::logic_error("Can't determine signedness of missing type");
  }
  if (type->kind() == ty::Kind::Integer) {
    auto* int_ty = dynamic_cast<ty::IntegerType*>(type);
    return int_ty->is_signed();
  }
  throw std::logic_error("Can't determine signedness of non-integer type");
}

auto binary_sign_aware(auto& base, auto&& s_fn, auto&& u_fn, const auto& args, auto&&... extra) {
  const auto& lhs = args.at(0);
  const auto& rhs = args.at(1);
  return (is_signed_type(lhs.type()) ? (base.*s_fn)(lhs, rhs, "", extra...) : (base.*u_fn)(lhs, rhs, "", extra...));
}

auto Compiler::expression(const ast::CallExpr& expr, bool mut) -> Val {
  auto fns_by_name = vector<Fn*>();
  auto overloads = vector<std::pair<int, Fn*>>();
  auto name = expr.name();

  for (auto& fn : m_fns) {
    if (fn.m_ast_decl.name() == name) {
      fns_by_name.push_back(&fn);
    }
  }
  if (fns_by_name.empty()) {
    throw std::logic_error("No matching overload for "s + name);
  }

  vector<Val> args{};
  vector<llvm::Value*> llvm_args{};
  for (const auto& i : expr.args()) {
    auto arg = body_expression(i);
    args.push_back(arg);
    llvm_args.push_back(arg.llvm());
  }

  for (auto* fn : fns_by_name) {
    int compat = 0;
    const auto& fn_ast = fn->m_ast_decl;
    auto fn_arg_size = fn_ast.args().size();
    if (args.size() == fn_arg_size || (expr.args().size() >= fn_arg_size && fn_ast.varargs())) {
      unsigned i = 0;
      for (const auto& arg : args) {
        if (i >= fn_arg_size) {
          break;
        }
        auto i_compat = arg.m_type->compatibility(convert_type(fn_ast.args().begin()[i].type()));
        if (i_compat == 0) {
          compat = INT_MIN;
          break;
        }
        compat += i_compat;
        i++;
      }
      overloads.emplace_back(compat, fn);
    }
  }

  auto* selected = std::ranges::max_element(overloads)->second;
  llvm::Function* llvm_fn = nullptr;
  ty::Type* ret_type = &m_types.unknown;
  if (selected->m_ast_decl.ret().has_value()) {
    ret_type = &convert_type(*selected->m_ast_decl.ret());
  }

  auto* ret_val = [&]() -> llvm::Value* {
    if (selected->m_ast_decl.primitive()) {
      auto primitive = get<string>(selected->m_ast_decl.body());
      if (primitive == "libc") {
        llvm_fn = selected->declaration(*this, false);
      } else if (primitive == "icmp_gt") {
        return binary_sign_aware(*m_builder, &IRBuilder<>::CreateICmpSGT, &IRBuilder<>::CreateICmpUGT, args);
      } else if (primitive == "icmp_lt") {
        return binary_sign_aware(*m_builder, &IRBuilder<>::CreateICmpSLT, &IRBuilder<>::CreateICmpULT, args);
      } else if (primitive == "icmp_eq") {
        return m_builder->CreateICmpEQ(args.at(0), args.at(1));
      } else if (primitive == "icmp_ne") {
        return m_builder->CreateICmpNE(args.at(0), args.at(1));
      } else if (primitive == "add") {
        return m_builder->CreateAdd(args.at(0), args.at(1));
      } else if (primitive == "mul") {
        return m_builder->CreateMul(args.at(0), args.at(1));
      } else if (primitive == "mod") {
        return binary_sign_aware(*m_builder, &IRBuilder<>::CreateSRem, &IRBuilder<>::CreateURem, args);
      } else if (primitive == "int_div") {
        return binary_sign_aware(*m_builder, &IRBuilder<>::CreateSDiv, &IRBuilder<>::CreateUDiv, args, false);
      } else {
        throw std::runtime_error("Unknown primitive "s + primitive);
      }
    } else {
      llvm_fn = selected->declaration(*this);
    }

    return m_builder->CreateCall(llvm_fn, llvm_args);
  }();

  return {ret_val, ret_type};
}

auto Compiler::expression(const ast::AssignExpr& expr, bool mut) -> Val {
  if (expr.target().kind() == ast::Kind::VarKind) {
    const auto& target_var = dynamic_cast<const ast::VarExpr&>(expr.target());
    auto expr_val = body_expression(expr.value());
    auto target_val = m_scope.at(target_var.name());
    m_builder->CreateStore(expr_val, target_val);
    return expr_val;
  }
  throw std::runtime_error("Can't assign to target "s + ast::kind_name(expr.kind()));
}

auto Compiler::expression(const ast::CtorExpr& expr, bool mut) -> Val {
  auto& type = known_type(expr.name());

  return {llvm::UndefValue::get(llvm_type(type)), &type}; // TODO
}

auto Compiler::expression(const ast::FieldAccessExpr& expr, bool mut) -> Val {
  auto base = body_expression(expr.base());
  auto& type = *base.type();

  if (type.kind() == ty::Kind::Struct) {
    auto& struct_type = dynamic_cast<ty::StructType&>(type);
    // auto* llvm_struct_type = llvm_type(struct_type, true);
    auto target_name = expr.field();
    ty::Type* target_type{};
    unsigned int i = 0;
    for (const auto& field : struct_type.fields()) {
      if (field.name() == target_name) {
        target_type = &convert_type(field.type());
        break;
      }
      i++;
    }

    // auto* base_struct = m_builder->CreateLoad(llvm_struct_type, base);
    auto* field = m_builder->CreateExtractValue(base, i, "s.field."s + target_name);

    return {field, target_type};
  }

  throw std::runtime_error("Can't access field of expression with non-struct type");
}

auto Compiler::body_expression(const ast::Expr& expr, bool mut) -> Val {
  auto kind = expr.kind();
  switch (kind) {
  case ast::NumberKind: return expression(dynamic_cast<const ast::NumberExpr&>(expr), mut);
  case ast::StringKind: return expression(dynamic_cast<const ast::StringExpr&>(expr), mut);
  case ast::CharKind: return expression(dynamic_cast<const ast::CharExpr&>(expr), mut);
  case ast::CallKind: return expression(dynamic_cast<const ast::CallExpr&>(expr), mut);
  case ast::VarKind: return expression(dynamic_cast<const ast::VarExpr&>(expr), mut);
  case ast::AssignKind: return expression(dynamic_cast<const ast::AssignExpr&>(expr), mut);
  case ast::CtorKind: return expression(dynamic_cast<const ast::CtorExpr&>(expr), mut);
  case ast::FieldAccessKind: return expression(dynamic_cast<const ast::FieldAccessExpr&>(expr), mut);
  default: throw std::logic_error("Unimplemented body expression "s + kind_name(kind));
  }
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

auto Compiler::mangle_name(const ast::FnDecl& fn_decl) -> string {
  std::stringstream ss{};
  ss << "_Ym.";
  ss << fn_decl.name();
  ss << "(";
  int idx = 0;
  for (const auto& i : fn_decl.args()) {
    if (idx++ > 0) {
      ss << ",";
    }
    ss << mangle_name(i.type());
  }
  ss << ")";
  // TODO: should mangled names even contain the return type...?
  if (fn_decl.ret().has_value()) {
    ss << mangle_name(fn_decl.ret().value());
  }
  return ss.str();
}

auto Compiler::mangle_name(const ast::Type& ast_type) -> string {
  if (ast_type.kind() == ast::Kind::SimpleTypeKind) {
    const auto& simple_type = dynamic_cast<const ast::SimpleType&>(ast_type);
    return simple_type.name();
  }
  std::stringstream ss{};
  const auto& qual_type = dynamic_cast<const ast::QualType&>(ast_type);
  auto qualifier = qual_type.qualifier();
  ss << mangle_name(qual_type.base());
  switch (qualifier) {
  case ast::QualType::Qualifier::Ptr: ss << "*"; break;
  case ast::QualType::Qualifier::Slice: ss << "["; break;
  }
  return ss.str();
}
auto Fn::declaration(Compiler& compiler, bool mangle) -> llvm::Function* {
  if (m_llvm_fn == nullptr) {
    m_llvm_fn = compiler.declare(*this, mangle);
  }
  return m_llvm_fn;
}
} // namespace yume
