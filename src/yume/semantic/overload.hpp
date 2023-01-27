#pragma once

#include "compiler/vals.hpp"
#include "diagnostic/notes.hpp"
#include "token.hpp"
#include "ty/compatibility.hpp"
#include "ty/substitution.hpp"
#include "util.hpp"
#include <llvm/Support/raw_ostream.h>
#include <utility>
#include <vector>

namespace yume::ast {
class AST;
} // namespace yume::ast
namespace llvm {
class raw_ostream;
}

namespace yume::semantic {

struct Overload {
  Fn* fn{};
  vector<ty::Compat> compatibilities{};
  Substitutions subs;
  bool viable = false;

  Overload() = delete;
  explicit Overload(Fn* fn) noexcept : fn{fn}, subs{fn->subs.deep_copy()} {}

  [[nodiscard]] auto better_candidate_than(Overload other) const -> bool;
  void dump(llvm::raw_ostream& stream) const;

  [[nodiscard]] auto location() const -> Loc { return fn->ast().location(); }
};

struct OverloadSet {
  ast::AST* call;
  vector<Overload> overloads;
  vector<ast::AST*> args;
  unique_ptr<diagnostic::StringNotesHolder> notes = std::make_unique<diagnostic::StringNotesHolder>();

  [[nodiscard]] auto empty() const -> bool { return overloads.empty(); }
  void dump(llvm::raw_ostream& stream, bool hide_invalid = false) const;
  void determine_valid_overloads();
  [[nodiscard]] auto is_valid_overload(Overload& overload) -> bool;
  [[nodiscard]] auto try_best_viable_overload() const -> const Overload*;
  [[nodiscard]] auto best_viable_overload() const -> Overload;
};

} // namespace yume::semantic
