#include "overload.hpp"
#include "ast/ast.hpp"
#include "ty/compatibility.hpp"
#include "ty/type.hpp"
#include "util.hpp"
#include <algorithm>
#include <compare>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace yume::semantic {

inline static constexpr auto get_val_ty = [](const ast::AST* ast) { return ast->val_ty(); };
inline static constexpr auto indirect = [](const ty::Type& ty) { return &ty; };

static auto join_args(const auto& iter, auto fn, llvm::raw_ostream& stream = errs()) {
  for (auto& i : llvm::enumerate(iter)) {
    if (i.index() != 0)
      stream << ", ";
    stream << fn(i.value())->name();
  }
}

static auto overload_name(const ast::AST* ast) -> std::string {
  if (const auto* call = dyn_cast<ast::CallExpr>(ast))
    return call->name();
  if (const auto* ctor = dyn_cast<ast::CtorExpr>(ast))
    return ctor->ensure_ty().name() + ":new";

  llvm_unreachable("Cannot evaluate overload set against non-call, non-ctor");
}

void Overload::dump(llvm::raw_ostream& stream) const {
  stream << fn->ast().location().to_string() << "\t" << fn->name() << "(";
  join_args(fn->arg_types(), indirect, stream);
  stream << ")";
  if (!subs.empty()) {
    stream << " with ";
    int i = 0;
    for (const auto& [k, v] : subs) {
      if (i++ > 0)
        stream << ", ";

      stream << k << " = " << v.name();
    }
  }
}

void OverloadSet::dump(llvm::raw_ostream& stream, bool hide_invalid) const {
  stream << overload_name(call) << "(";
  join_args(args, get_val_ty, stream);
  stream << ")\n";
  for (const auto& i_s : overloads) {
    if (hide_invalid && !i_s.viable)
      continue;
    stream << "  ";
    i_s.dump(stream);
    stream << "\n";
  }
}

static auto literal_cast(ast::AST& arg, ty::Type target_type) -> ty::Compat {
  if (arg.val_ty() == target_type)
    return {.valid = true}; // Already the correct type

  if (isa<ast::NumberExpr>(arg) && target_type.base_isa<ty::Int>()) {
    auto& num_arg = cast<ast::NumberExpr>(arg);
    const auto* int_type = target_type.base_cast<ty::Int>();

    if (int_type->size() == 1)
      return {}; // Can't implicitly cast to Bool

    auto in_range = int_type->in_range(num_arg.val());

    if (in_range)
      return {.valid = true, .conv = {.dereference = false, .kind = ty::Conv::Int}};
  }

  return {};
}

auto parameter_count_matches(const vector<ast::AST*>& args, const Fn& fn) -> bool {
  // Varargs functions may have more arguments that the amount of non-vararg parameters.
  if (args.size() != fn.arg_count())
    if (!fn.varargs() || args.size() < fn.arg_count())
      return false;
  return true;
}

auto OverloadSet::is_valid_overload(Overload& overload) const -> bool {
  const auto& fn = *overload.fn;

  // The overload is only viable if the amount of arguments matches the amount of parameters.
  if (!parameter_count_matches(args, fn))
    return false;

  overload.compatibilities.reserve(args.size());

  // Determine the type compatibility of each argument individually. The performed conversions are also recorded for
  // each step.
  // As `llvm::zip` only iterates up to the size of the shorter argument, we don't try to determine type
  // compatibility of the "variadic" part of varargs functions. Currently, varargs methods can only be primitives and
  // carry no type information for their variadic part. This will change in the future.
  for (const auto& [param_type_r, arg] : llvm::zip_first(fn.arg_types(), args)) {
    auto arg_type = arg->val_ty();
    optional<ty::Type> param_type = param_type_r;

    if (param_type->is_generic()) {
      auto sub = arg_type->determine_generic_subs(*param_type, overload.subs);

      // No valid substitution found
      if (!sub)
        return false;

      param_type = param_type->apply_generic_substitution(*sub);

      if (!param_type)
        return false;
    }

    // Attempt to do a literal cast
    auto compat = literal_cast(*arg, *param_type);
    // Couldn't perform a literal cast, try regular casts
    if (!compat.valid)
      compat = arg_type->compatibility(*param_type);

    // Couldn't perform any kind of valid cast: one invalid conversion disqualifies the function entirely
    if (!compat.valid)
      return false;

    // Save the steps needed to perform the conversion
    overload.compatibilities.push_back(compat);
  }

  // Add dummy conversions for each argument which maps to a variadic
  while (overload.compatibilities.size() < args.size())
    overload.compatibilities.emplace_back();

  // Must be valid!
  return true;
}

void OverloadSet::determine_valid_overloads() {
  // All `Overload`s are determined to not be viable by default, so determine the ones which actually are
  // TODO(rymiel): #17 Actually keep track of *why* a type is not viable, for diagnostics.
  for (auto& i : overloads)
    i.viable = is_valid_overload(i);
}

static auto cmp(bool a, bool b) -> std::strong_ordering { return static_cast<int>(a) <=> static_cast<int>(b); }

static auto compare_implicit_conversions(ty::Conv a, ty::Conv b) -> std::weak_ordering {
  const auto& equal = std::strong_ordering::equal;

  // No conversion is better than some conversion
  if (auto c = cmp(a.kind == ty::Conv::None, b.kind == ty::Conv::None); c != equal)
    return c;

  // No dereference is better than performing a dereference
  if (auto c = cmp(!a.dereference, !b.dereference); c != equal)
    return c;

  // Cannot distinguish!
  return equal;
}

auto Overload::better_candidate_than(Overload other) const -> bool {
  // Viable candidates are always better than non-viable ones
  if (!other.viable)
    return viable;
  if (!viable)
    return false;

  // For each argument, determine which candidate has a "better" conversion.
  for (const auto& [self_compat, other_compat] : llvm::zip_first(compatibilities, other.compatibilities)) {
    auto comparison = compare_implicit_conversions(self_compat.conv, other_compat.conv);

    if (is_gt(comparison))
      return true;
    if (is_lt(comparison))
      return false;

    // Cannot distinguish between these, try the next arguments
  }

  // If we got to here, it means all arguments were identical. Neither overload is better than the other
  return false;
}

auto OverloadSet::try_best_viable_overload() const -> const Overload* {
  const Overload* best = nullptr;

  for (const auto& candidate : overloads)
    if (candidate.viable)
      if (best == nullptr || candidate.better_candidate_than(*best))
        best = &candidate;

  return best;
}

auto OverloadSet::best_viable_overload() const -> Overload {
  const auto* best = try_best_viable_overload();

  if (best == nullptr) {
    string str{};
    llvm::raw_string_ostream ss{str};
    ss << "No viable overload for " << overload_name(call) << " with argument types ";
    join_args(args, get_val_ty, ss);
    ss << "\nNone of the following overloads were suitable:\n";
    for (const auto& i : overloads) {
      i.dump(ss);
      ss << "\n";
    }
    throw std::logic_error(str);
  }

  vector<const Overload*> ambiguous;

  for (const auto& candidate : overloads)
    if (candidate.viable && &candidate != best)
      if (!best->better_candidate_than(candidate))
        ambiguous.push_back(&candidate);

  if (ambiguous.empty())
    return *best;

  ambiguous.push_back(best);

  string str{};
  llvm::raw_string_ostream ss{str};
  ss << "Ambiguous call for " << overload_name(call) << " with argument types ";
  join_args(args, get_val_ty, ss);
  ss << "\nCouldn't pick between the following overloads:\n";
  for (const auto* i : ambiguous) {
    i->dump(ss);
    ss << "\n";
  }

  throw std::logic_error(str);
}

} // namespace yume::semantic
