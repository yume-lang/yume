//
// Created by rymiel on 5/8/22.
//

#include "compiler.hpp"

namespace yume {
Compiler::Compiler() {
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
}

void Compiler::add_main() {
  FunctionType* fn_t = FunctionType::get(Type::getInt32Ty(*m_context), vector<Type*>(), false);

  Function* fn = Function::Create(fn_t, Function::ExternalLinkage, "main", m_module.get());

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