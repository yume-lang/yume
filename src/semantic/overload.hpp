#pragma once

#include "compatibility.hpp"
#include "compiler/vals.hpp"
#include <vector>

namespace yume::ast {
class CallExpr;
}
namespace yume::ty {
class Type;
}

namespace yume::semantic {

struct Overload {
  Fn* fn{};
  vector<ty::Compatiblity> compatibilities{};
  Instantiation instantiation{};
  bool viable = false;

  Overload() = default;
  explicit Overload(Fn* fn_) : fn{fn_} {}

  [[nodiscard]] auto better_candidate_than(Overload other) const -> bool;
  void dump(llvm::raw_ostream& stream) const;
};

struct OverloadSet {
  ast::CallExpr& call;
  vector<Overload> overloads;
  vector<const ty::Type*> arg_types;

  [[nodiscard]] auto empty() const -> bool { return overloads.empty(); }
  void dump(llvm::raw_ostream& stream, bool hide_invalid = false) const;
  void determine_valid_overloads();
  [[nodiscard]] auto is_valid_overload(Overload& overload) -> bool;
  [[nodiscard]] auto best_viable_overload() const -> Overload;
};

} // namespace yume::semantic
