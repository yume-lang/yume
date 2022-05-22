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

  for (const auto& i : m_program->body()) {
    if (i->kind() == ast::Kind::FnDecl) {
      add(dynamic_cast<const ast::FnDeclStatement&>(*i));
    }
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

void Compiler::add(const ast::FnDeclStatement& fn_decl) {
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

  BasicBlock* bb = BasicBlock::Create(*m_context, "entry", fn);
  m_builder->SetInsertPoint(bb);
  m_builder->CreateRet(m_builder->getInt32(0));
  verifyFunction(*fn);
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