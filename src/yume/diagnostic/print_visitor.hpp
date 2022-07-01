#pragma once

#include "util.hpp"
#include "visitor.hpp"
#include <iosfwd>
#include <string>

namespace yume::ast {
class AST;
}
namespace llvm {
class raw_ostream;
}

namespace yume::diagnostic {
class PrintVisitor : public Visitor {
  llvm::raw_ostream& m_stream;
  bool m_needs_sep = false;

  void header(const char* label);

public:
  explicit PrintVisitor(llvm::raw_ostream& stream_) : m_stream{stream_} {}
  ~PrintVisitor() override = default;

  PrintVisitor(const PrintVisitor&) = delete;
  PrintVisitor(PrintVisitor&&) = delete;
  auto operator=(const PrintVisitor&) -> PrintVisitor& = delete;
  auto operator=(PrintVisitor&&) -> PrintVisitor& = delete;

  auto visit(const ast::AST& expr, const char* label) -> PrintVisitor& override;

  auto visit(std::nullptr_t null, const char* label) -> PrintVisitor& override;

  auto visit(const string& str, const char* label) -> PrintVisitor& override;
};
} // namespace yume::diagnostic
