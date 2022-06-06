#pragma once

#include "../visitor.hpp"
#include "crtp_walker.hpp"
#include <string>

namespace yume {
class Compiler;
struct Fn;
namespace ast {
class AST;
}

struct TypeWalkVisitor : public CRTPWalker<TypeWalkVisitor> {
  friend CRTPWalker;

public:
  Compiler& m_compiler;
  Fn* m_current_fn{};
  // Whether or not to compile the bodies of methods.  Initially, on the parameter types of methods are traversed and
  // converted, then everything else in a second pass.
  bool m_in_depth = false;

  explicit TypeWalkVisitor(Compiler& compiler) : m_compiler(compiler) {}

private:
  template <typename T> inline void statement([[maybe_unused]] T& stat) {
    // ignore
  }

  template <typename T> inline void expression([[maybe_unused]] T& expr) {
    // ignore
  }
};
} // namespace yume
