#include "vals.hpp"
#include "compiler.hpp"

namespace yume {

auto Fn::declaration(Compiler& compiler, bool mangle) -> llvm::Function* {
  if (m_llvm_fn == nullptr) {
    m_llvm_fn = compiler.declare(*this, mangle);
  }
  return m_llvm_fn;
}
} // namespace yume
