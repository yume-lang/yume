#include "errors.hpp"

#include "ast/ast.hpp"
#include "token.hpp"
#include "util.hpp"
#include <utility>

namespace yume {
ASTStackTrace::ASTStackTrace(std::string message) : m_message(std::move(message)) {}

ASTStackTrace::ASTStackTrace(std::string message, const ast::AST& ast) : m_message(std::move(message)) {
  m_message += " (" + ast.location().to_string() + ")";
}
} // namespace yume
