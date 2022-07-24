#pragma once

#include "util.hpp"
#include <map>

namespace yume {
namespace ty {
class Type;
}

using substitution_t = std::map<string, const ty::Type*>;
} // namespace yume
