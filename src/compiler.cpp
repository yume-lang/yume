//
// Created by rymiel on 5/8/22.
//

#include "compiler.hpp"

namespace yume {
using namespace std::literals::string_literals;

Compiler::Compiler(unique_ptr<ast::Program> program) : m_program(move(program)) {
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

  for (auto i : {8, 16, 32, 64}) {
    m_known_types.insert({"I"s + std::to_string(i), std::make_unique<ty::IntegerType>(i, true)});
    m_known_types.insert({"U"s + std::to_string(i), std::make_unique<ty::IntegerType>(i, false)});
  }

  vector<Fn*> pending_body{};

  for (const auto& i : m_program->body()) {
    if (i->kind() == ast::Kind::FnDecl) {
      auto& fn_decl_ptr = dynamic_cast<ast::FnDeclStatement&>(*i);
      auto* llvm_fn = declare(fn_decl_ptr);
      auto& r = m_fn_decls.emplace_back(std::make_unique<Fn>(fn_decl_ptr, llvm_fn));
      pending_body.push_back(r.get());
    }
  }

  for (auto* i : pending_body) {
    define(*i);
  }
}

auto Compiler::convert_type(const ast::Type& ast_type) -> ty::Type& {
  static unique_ptr<ty::Type> unknown_type = std::make_unique<ty::UnknownType>();

  if (ast_type.kind() == ast::Kind::SimpleType) {
    const auto& simple_type = dynamic_cast<const ast::SimpleType&>(ast_type);
    auto name = simple_type.name();
    auto val = m_known_types.find(name);
    if (val != m_known_types.end()) {
      return *val->second;
    }
  } else {
    const auto& qual_type = dynamic_cast<const ast::QualType&>(ast_type);
    auto qualifier = qual_type.qualifier();
    return convert_type(*qual_type.base()).known_qual(qualifier);
  }

  return *unknown_type;
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
      auto args = vector<Type*>{};
      args.push_back(llvm::PointerType::getUnqual(llvm_type(qual_type.base())));
      args.push_back(llvm::Type::getInt64PtrTy(*m_context));
      return llvm::StructType::get(*m_context, args);
    }
    default: return llvm_type(qual_type.base());
    }
  }

  return Type::getVoidTy(*m_context);
}

auto Compiler::declare(const ast::FnDeclStatement& fn_decl) -> Function* {
  auto* ret_type = llvm::Type::getVoidTy(*m_context);
  auto args = vector<llvm::Type*>{};
  if (fn_decl.ret()) {
    ret_type = llvm_type(convert_type(*fn_decl.ret().value()));
  }
  for (const auto& i : fn_decl.args()) {
    args.push_back(llvm_type(convert_type(*i->type())));
  }
  llvm::FunctionType* fn_t = llvm::FunctionType::get(ret_type, args, fn_decl.varargs());

  string name = fn_decl.name();
  if (name != "main") {
    name = mangle_name(fn_decl);
  }

  Function* fn = Function::Create(fn_t, Function::ExternalLinkage, name, m_module.get());

  int arg_i = 0;
  for (auto& arg : fn->args()) {
    arg.setName(fn_decl.args()[arg_i]->name());
    arg_i++;
  }

  return fn;
}

void Compiler::define(Fn& fn) {
  m_current_fn = &fn;
  BasicBlock* bb = BasicBlock::Create(*m_context, "entry", fn);
  m_builder->SetInsertPoint(bb);

  if (const auto* body = get_if<unique_ptr<ast::Compound>>(&fn.body()); body != nullptr) {
    statement(**body);
  }
  // m_builder->CreateRet(m_builder->getInt32(0));
  verifyFunction(*fn, &llvm::errs());
}

void Compiler::statement(const ast::Compound& stat) {
  for (const auto& i : stat.body()) {
    body_statement(*i);
  }
}

void Compiler::statement(const ast::WhileStatement& stat) {}

void Compiler::statement(const ast::IfStatement& stat) {}

void Compiler::statement(const ast::ExprStatement& stat) {}

void Compiler::statement(const ast::ReturnStatement& stat) {
  if (stat.expr().has_value()) {
    auto* val = body_expression(**stat.expr());
    m_builder->CreateRet(val);
    return;
  }
  m_builder->CreateRetVoid();
}

void Compiler::statement(const ast::VarDeclStatement& stat) {
  llvm::Type* var_type = nullptr;
  if (stat.type().has_value()) {
    var_type = llvm_type(convert_type(**stat.type()));
  }

  auto* alloc = m_builder->CreateAlloca(var_type, nullptr, stat.name());
  auto* val = body_expression(*stat.init());
  m_current_fn->m_scope.insert({stat.name(), alloc});
  m_builder->CreateStore(val, alloc);
}

void Compiler::body_statement(const ast::Statement& stat) {
  auto kind = stat.kind();
  switch (kind) {
  case ast::Kind::Compound: return statement(dynamic_cast<const ast::Compound&>(stat));
  case ast::Kind::WhileStatement: return statement(dynamic_cast<const ast::WhileStatement&>(stat));
  case ast::Kind::IfStatement: return statement(dynamic_cast<const ast::IfStatement&>(stat));
  case ast::Kind::ExprStatement: return statement(dynamic_cast<const ast::ExprStatement&>(stat));
  case ast::Kind::ReturnStatement: return statement(dynamic_cast<const ast::ReturnStatement&>(stat));
  case ast::Kind::VarDecl: return statement(dynamic_cast<const ast::VarDeclStatement&>(stat));
  default: throw std::logic_error("Unimplemented body statement "s + kind_name(kind));
  }
}

auto Compiler::expression(const ast::NumberExpr& expr) -> llvm::Value* {
  auto val = expr.val();
  if (val > std::numeric_limits<int32_t>::max()) {
    return m_builder->getInt64(val);
  }
  return m_builder->getInt32(val);
}

auto Compiler::expression(const ast::VarExpr& expr) -> llvm::Value* {
  auto* val = m_current_fn->m_scope.find(expr.name())->second;
  return m_builder->CreateLoad(val->getType()->getPointerElementType(), val);
}

auto Compiler::body_expression(const ast::Expr& expr) -> llvm::Value* {
  auto kind = expr.kind();
  switch (kind) {
  case ast::Kind::Number:
    return expression(dynamic_cast<const ast::NumberExpr&>(expr));
    //  case ast::Kind::String: break;
    //  case ast::Kind::Call: break;
  case ast::Kind::Var:
    return expression(dynamic_cast<const ast::VarExpr&>(expr));
    //  case ast::Kind::Assign: break;
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

auto Compiler::mangle_name(const ast::FnDeclStatement& fn_decl) -> string {
  std::stringstream ss{};
  ss << "_Ym.";
  ss << fn_decl.name();
  ss << "(";
  int idx = 0;
  for (const auto& i : fn_decl.args()) {
    if (idx++ > 0) {
      ss << ",";
    }
    ss << mangle_name(*i->type());
  }
  ss << ")";
  if (auto* ret = fn_decl.ret()->get(); ret != nullptr) {
    ss << mangle_name(*ret);
  }
  return ss.str();
}

auto Compiler::mangle_name(const ast::Type& ast_type) -> string {
  if (ast_type.kind() == ast::Kind::SimpleType) {
    const auto& simple_type = dynamic_cast<const ast::SimpleType&>(ast_type);
    return simple_type.name();
  }
  std::stringstream ss{};
  const auto& qual_type = dynamic_cast<const ast::QualType&>(ast_type);
  auto qualifier = qual_type.qualifier();
  ss << mangle_name(*qual_type.base());
  switch (qualifier) {
  case ast::QualType::Qualifier::Ptr: ss << "*"; break;
  case ast::QualType::Qualifier::Slice: ss << "["; break;
  }
  return ss.str();
}
} // namespace yume