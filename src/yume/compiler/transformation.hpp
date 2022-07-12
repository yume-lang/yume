#pragma once

#include "ast/ast.hpp"

namespace yume {
auto transform_enum(ast::EnumDecl& decl) -> unique_ptr<ast::StructDecl>;
}
