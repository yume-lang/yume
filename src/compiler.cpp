//
// Created by rymiel on 5/8/22.
//

#include "compiler.hpp"
#include <sstream>

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

  for (const auto& i : m_program->body()) {
    if (i.kind() == ast::Kind::FnDecl) {
      const auto& fn_decl = dynamic_cast<const ast::FnDeclStatement&>(i);
      auto& fn = m_fns.emplace_back(fn_decl);
      if (fn_decl.name() == "main") {
        fn.m_llvm_fn = declare(fn, false);
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
    return convert_type(qual_type.base()).known_qual(qualifier);
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
    arg.setName(fn_decl.args().begin()[arg_i].name());
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

  if (const auto* body = get_if<unique_ptr<ast::Compound>>(&fn.body()); body != nullptr) {
    statement(**body);
  }
  // m_builder->CreateRet(m_builder->getInt32(0));
  // verifyFunction(*fn, &llvm::errs());
}

void Compiler::statement(const ast::Compound& stat) {
  for (const auto& i : stat.body()) {
    body_statement(i);
  }
}

void Compiler::statement(const ast::WhileStatement& stat) {}

void Compiler::statement(const ast::IfStatement& stat) {}

void Compiler::statement(const ast::ExprStatement& stat) { body_expression(stat.expr()); }

void Compiler::statement(const ast::ReturnStatement& stat) {
  if (stat.expr().has_value()) {
    auto val = body_expression(stat.expr().value());
    m_builder->CreateRet(val);
    return;
  }
  m_builder->CreateRetVoid();
}

void Compiler::statement(const ast::VarDeclStatement& stat) {
  llvm::Type* var_type = nullptr;
  if (stat.type().has_value()) {
    var_type = llvm_type(convert_type(stat.type().value()));
  }

  auto* alloc = m_builder->CreateAlloca(var_type, nullptr, stat.name());
  auto val = body_expression(stat.init());
  m_scope.insert({stat.name(), alloc});
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

auto Compiler::expression(const ast::NumberExpr& expr) -> Val {
  auto val = expr.val();
  if (val > std::numeric_limits<int32_t>::max()) {
    return m_builder->getInt64(val);
  }
  return m_builder->getInt32(val);
}

auto Compiler::expression(const ast::VarExpr& expr) -> llvm::Value* {
  auto* val = m_scope.find(expr.name())->second;
  return m_builder->CreateLoad(val->getType()->getPointerElementType(), val);
  return m_builder->CreateLoad(val.llvm()->getType()->getPointerElementType(), val);
}

auto Compiler::expression(const ast::CallExpr& expr) -> Val {
  auto overloads = vector<Fn*>();
  auto name = expr.name();
  std::cerr << "Searching for call overload of " << name << "\nGot: ";
  for (auto& fn : m_fns) {
    if (fn.m_ast_decl.name() == name) {
      overloads.push_back(&fn);
    }
  }
  for (const auto* overload : overloads) {
    std::cerr << overload->m_ast_decl.describe() << ", ";
  }
  std::cerr << "\n";
  if (overloads.empty()) {
    throw std::logic_error("No matching overload for "s + name);
  }
  auto* selected = overloads.front();
  llvm::Function* llvm_fn = nullptr;
  if (selected->m_ast_decl.primitive()) {
    auto primitive = get<string>(selected->m_ast_decl.body());
    if (primitive == "libc") {
      llvm_fn = selected->declaration(*this, false);
    } else {
      return nullptr;
    }
  } else {
    llvm_fn = selected->declaration(*this);
  }

  vector<llvm::Value*> args{};
  for (const auto& i : expr.args()) {
    args.push_back(body_expression(i));
  }

  return m_builder->CreateCall(llvm_fn, args);
}

auto Compiler::body_expression(const ast::Expr& expr) -> llvm::Value* {
  auto kind = expr.kind();
  switch (kind) {
  case ast::Kind::Number:
    return expression(dynamic_cast<const ast::NumberExpr&>(expr));
    //  case ast::Kind::String: break;
  case ast::Kind::Call: return expression(dynamic_cast<const ast::CallExpr&>(expr));
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
  if (ast_type.kind() == ast::Kind::SimpleType) {
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
