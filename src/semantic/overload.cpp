#include "overload.hpp"
#include "ast/ast.hpp"
#include "token.hpp"
#include "type.hpp"
#include "util.hpp"
#include <algorithm>
#include <compare>
#include <functional>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace yume::semantic {

template <typename Fn = std::identity>
static auto join_args(const auto& iter, Fn fn = {}, llvm::raw_ostream& stream = llvm::errs()) {
  int j = 0;
  for (auto& i : iter) {
    if (j++ != 0)
      stream << ", ";
    stream << fn(i)->name();
  }
}

/// Add the substitution `gen_sub` for the generic type variable `gen` in template instantiation `instantiation`.
/// If a substitution already exists for the same type variable, the two substitutions are intersected.
/// \returns true if substitution failed
static auto intersect_generics(Instantiation& instantiation, const ty::Generic* gen, const ty::Type* gen_sub) -> bool {
  // TODO: this logic should be in the Type compatibility checker algorithm itself
  auto existing = instantiation.sub.find(gen);
  // The substitution must have an intersection with an already deduced value for the same type variable
  if (existing != instantiation.sub.end()) {
    const auto* intersection = gen_sub->intersect(*existing->second);
    if (intersection == nullptr) {
      // The types don't have a common intersection, they cannot coexist.
      return true;
    }
    instantiation.sub[gen] = intersection;
  } else {
    instantiation.sub.try_emplace(gen, gen_sub);
  }

  return false;
}

inline static constexpr auto get_val_ty = [](const ast::AST& ast) { return ast.val_ty(); };

void Overload::dump(llvm::raw_ostream& stream) const {
  stream << fn->ast().location().to_string() << "\t" << fn->name() << "(";
  join_args(fn->ast().args(), get_val_ty, stream);
  stream << ")";
  if (!instantiation.sub.empty()) {
    stream << " with ";
    int i = 0;
    for (const auto& [k, v] : instantiation.sub) {
      if (i++ > 0)
        stream << ", ";

      stream << k->name() << " = " << v->name();
    }
  }
};

void OverloadSet::dump(llvm::raw_ostream& stream, bool hide_invalid) const {
  stream << call.name() << "(";
  join_args(arg_types, {}, stream);
  stream << ")\n";
  for (const auto& i_s : overloads) {
    if (hide_invalid && !i_s.viable)
      continue;
    stream << "  ";
    i_s.dump(stream);
    stream << "\n";
  }
};

auto OverloadSet::is_valid_overload(Overload& overload) -> bool {
  const auto& fn_ast = overload.fn->m_ast_decl;

  // The overload is only viable if the amount of arguments matches the amount of parameters.
  // Varargs functions may have more arguments that the amount of non-vararg parameters.
  if (arg_types.size() != fn_ast.args().size())
    if (!fn_ast.varargs() || arg_types.size() < fn_ast.args().size())
      return false;

  overload.compatibilities.reserve(arg_types.size());

  // Determine the type compatibility of each argument individually. The performed conversions are also recorded for
  // each step.
  // As `llvm::zip` only iterates up to the size of the shorter argument, we don't try to determine type
  // compatibility of the "variadic" part of varargs functions. Currently, varargs methods can only be primitives and
  // carry no type information for their variadic part. This will change in the future.
  for (const auto& [ast_arg, arg_type] : llvm::zip_first(fn_ast.args(), arg_types)) {
    auto compat = arg_type->compatibility(*ast_arg.val_ty());
    // One invalid conversion disqualifies the function entirely
    if (!compat.valid)
      return false;

    // The function compatibility determined a substitution for a type variable.
    if (compat.substituted_generic != nullptr && compat.substituted_with != nullptr) {
      if (intersect_generics(overload.instantiation, compat.substituted_generic, compat.substituted_with)) {
        return false;
      }
    }

    // Save the steps needed to perform the conversion
    overload.compatibilities.push_back(compat);
  }

  // Add empty conversions for each argument which maps to a variadic
  while (overload.compatibilities.size() < arg_types.size())
    overload.compatibilities.emplace_back();

  // Must be valid!
  return true;
}

void OverloadSet::determine_valid_overloads() {
  auto& [call_expr, overloads, arg_types] = *this;

  // All `Overload`s are determined to not be viable by default, so determine the ones which actually are
  for (auto& i : overloads) {
    i.viable = is_valid_overload(i);
  }
}

static auto compare_implicit_conversions(ty::Conversion a, ty::Conversion b) -> std::strong_ordering {
  // TODO: a lot of this function is repetitive

  // Concrete arguments are always better than ones requiring generic substitution
  if (!a.generic && b.generic)
    return std::strong_ordering::greater;
  if (!b.generic && a.generic)
    return std::strong_ordering::less;

  // No conversion is better than some conversion
  if (a.kind == ty::ConversionKind::None && b.kind != ty::ConversionKind::None)
    return std::strong_ordering::greater;
  if (b.kind == ty::ConversionKind::None && a.kind != ty::ConversionKind::None)
    return std::strong_ordering::less;

  // No dereference is better than performing a dereference
  if (!a.dereference && b.dereference)
    return std::strong_ordering::greater;
  if (!b.dereference && a.dereference)
    return std::strong_ordering::less;

  // Cannot distinguish!
  return std::strong_ordering::equivalent;
}

auto Overload::better_candidate_than(Overload other) const -> bool {
  // Viable candidates are always better than non-viable ones
  if (!other.viable)
    return viable;
  if (!viable)
    return false;

  // For each argument, determine which candidate has a "better" conversion.
  for (const auto& [self_compat, other_compat] : llvm::zip_first(compatibilities, other.compatibilities)) {
    auto comparison = compare_implicit_conversions(self_compat.conversion, other_compat.conversion);

    if (is_gt(comparison))
      return true;
    if (is_lt(comparison))
      return false;

    // Cannot distinguish between these ones, try the next arguments
  }

  // If we got to here, it means all arguments were identical. Neither overload is better than the other
  return false;
}

auto OverloadSet::best_viable_overload() const -> Overload {
  const Overload* best = nullptr;

  for (const auto& candidate : overloads)
    if (candidate.viable)
      if (best == nullptr || candidate.better_candidate_than(*best))
        best = &candidate;

  if (best == nullptr) {
    string str{};
    llvm::raw_string_ostream ss{str};
    ss << "No viable overload for " << call.name() << " with argument types ";
    join_args(arg_types, {}, ss);
    throw std::logic_error(str);
  }

  std::vector<const Overload*> ambiguous;

  for (const auto& candidate : overloads)
    if (candidate.viable && &candidate != best)
      if (!best->better_candidate_than(candidate))
        ambiguous.push_back(&candidate);

  if (ambiguous.empty())
    return *best;

  ambiguous.push_back(best);

  string str{};
  llvm::raw_string_ostream ss{str};
  ss << "Ambigious call for " << call.name() << " with argument types ";
  join_args(arg_types, {}, ss);
  ss << "\nCouldn't pick between the following overloads:\n";
  for (const auto* i : ambiguous) {
    i->dump(ss);
    ss << "\n";
  }

  throw std::logic_error(str);
}
} // namespace yume::semantic
