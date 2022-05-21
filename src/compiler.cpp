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
    m_known_types.insert({"I"s + std::to_string(i), Type::getIntNTy(*m_context, i)});
    m_known_types.insert({"U"s + std::to_string(i), Type::getIntNTy(*m_context, i)});
  }

  for (const auto& i : m_program->body()) {
    if (i->kind() == ast::Kind::FnDecl) {
      add(dynamic_cast<const ast::FnDeclStatement&>(*i));
    }
  }
}

auto Compiler::convert_type(const ast::Type& ast_type) -> llvm::Type* {
  if (ast_type.kind() == ast::Kind::SimpleType) {
    const auto& simple_type = dynamic_cast<const ast::SimpleType&>(ast_type);
    auto name = simple_type.name();
    auto val = m_known_types.find(name);
    if (val != m_known_types.end()) {
      return val->second;
    }
  } else {
    const auto& qual_type = dynamic_cast<const ast::QualType&>(ast_type);
    auto qualifier = qual_type.qualifier();
    switch (qualifier) {
    case ast::QualType::Qualifier::Ptr: return PointerType::getUnqual(convert_type(*qual_type.base()));
    case ast::QualType::Qualifier::Slice: {
      auto args = vector<Type*>{};
      args.push_back(PointerType::getUnqual(convert_type(*qual_type.base())));
      args.push_back(Type::getInt64PtrTy(*m_context));
      return StructType::get(*m_context, args);
    }
    default: return convert_type(*qual_type.base());
    }
  }

  return Type::getVoidTy(*m_context);
}

void Compiler::add(const ast::FnDeclStatement& fn_decl) {
  auto* ret_type = Type::getVoidTy(*m_context);
  auto args = vector<Type*>{};
  if (fn_decl.ret().has_value()) {
    ret_type = convert_type(**fn_decl.ret());
  }
  for (const auto& i : fn_decl.args()) {
    args.push_back(convert_type(*i->type()));
  }
  FunctionType* fn_t = FunctionType::get(ret_type, args, fn_decl.varargs());

  Function* fn = Function::Create(fn_t, Function::ExternalLinkage, fn_decl.name(), m_module.get());

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
} // namespace yume