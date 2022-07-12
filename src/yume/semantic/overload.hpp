#pragma once

#include "compiler/vals.hpp"
#include "ty/compatibility.hpp"
#include <vector>

namespace yume::ast {
class CallExpr;
class AST;
} // namespace yume::ast
namespace llvm {
class raw_ostream;
}

namespace yume::semantic {

struct Overload {
  Fn* fn{};
  vector<ty::Compat> compatibilities{};
  Instantiation instantiation{};
  bool viable = false;

  Overload() = default;
  explicit Overload(Fn* fn) : fn{fn} {}

  [[nodiscard]] auto better_candidate_than(Overload other) const -> bool;
  void dump(llvm::raw_ostream& stream) const;
};

struct OverloadSet {
  ast::CallExpr& call;
  vector<Overload> overloads;
  vector<ast::AST*> args;

  [[nodiscard]] auto empty() const -> bool { return overloads.empty(); }
  void dump(llvm::raw_ostream& stream, bool hide_invalid = false) const;
  void determine_valid_overloads();
  [[nodiscard]] auto is_valid_overload(Overload& overload) -> bool;
  [[nodiscard]] auto best_viable_overload() const -> Overload;
};

} // namespace yume::semantic
