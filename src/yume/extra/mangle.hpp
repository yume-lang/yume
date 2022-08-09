#pragma once

#include "compiler/vals.hpp"

namespace yume::mangle {
auto mangle_name(Fn& fn) -> string;
auto mangle_name(Ctor& ctor) -> string;
auto mangle_name(ty::Type ast_type, DeclLike parent) -> string;
} // namespace yume::mangle
