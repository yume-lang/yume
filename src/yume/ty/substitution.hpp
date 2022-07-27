#pragma once

#include "ty/type_base.hpp"
#include "util.hpp"
#include <map>

namespace yume {
namespace ty {
class Generic;
struct Sub {
  const Generic* target{};
  Type replace;

  auto constexpr operator==(const Sub& other) const noexcept -> bool = default;
};
} // namespace ty

using substitution_t = std::map<string, ty::Type>;
struct Substitution : substitution_t {
  using substitution_t::map;

  auto operator<=>(const Substitution& other) const noexcept = default;
};
} // namespace yume
