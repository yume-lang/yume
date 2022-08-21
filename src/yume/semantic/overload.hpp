#pragma once

#include "compiler/vals.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include "util.hpp"
#include <vector>

namespace yume::ast {
class AST;
} // namespace yume::ast
namespace llvm {
class raw_ostream;
}

namespace yume::semantic {

template <typename T> struct Overload {
  T* fn{};
  vector<ty::Compat> compatibilities{};
  Substitution subs{};
  bool viable = false;

  Overload() = default;
  explicit Overload(T* fn) noexcept : fn{fn} {}

  [[nodiscard]] auto better_candidate_than(Overload other) const -> bool;
  void dump(llvm::raw_ostream& stream) const;
};

template <typename T> struct OverloadSet {
  using call_t = typename T::call_t;
  call_t* call;
  vector<Overload<T>> overloads;
  vector<ast::AST*> args;

  [[nodiscard]] auto empty() const -> bool { return overloads.empty(); }
  void dump(llvm::raw_ostream& stream, bool hide_invalid = false) const;
  void determine_valid_overloads();
  [[nodiscard]] auto is_valid_overload(Overload<T>& overload) -> bool;
  [[nodiscard]] auto try_best_viable_overload() const -> const Overload<T>*;
  [[nodiscard]] auto best_viable_overload() const -> Overload<T>;
};

} // namespace yume::semantic
