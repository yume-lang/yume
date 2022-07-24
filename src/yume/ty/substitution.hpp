#pragma once

#include "util.hpp"
#include <map>

namespace yume {
namespace ty {
class Type;
}

using substitution_t = std::map<string, const ty::Type*>;
struct Substitution : substitution_t {
  using substitution_t::map;

  auto operator<=>(const Substitution& other) const = default;
};
} // namespace yume
