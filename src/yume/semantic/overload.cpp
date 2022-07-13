#include "overload.hpp"
#include "ast/ast.hpp"
#include "token.hpp"
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
#include <variant>

namespace yume::semantic {

inline static constexpr auto get_val_ty = [](const ast::AST* ast) { return ast->val_ty(); };

static auto join_args(const auto& iter, auto fn, llvm::raw_ostream& stream = llvm::errs()) {
  for (auto& i : llvm::enumerate(iter)) {
    if (i.index() != 0)
      stream << ", ";
    stream << fn(i.value())->name();
  }
}

/// Add the substitution `gen_sub` for the generic type variable `gen` in template instantiation `instantiation`.
/// If a substitution already exists for the same type variable, the two substitutions are intersected.
/// \returns nullptr if substitution failed
static auto intersect_generics(Instantiation& instantiation, const ty::Generic* gen, const ty::Type* gen_sub)
    -> const ty::Type* {
  // TODO: this logic should be in the Type compatibility checker algorithm itself
  auto existing = instantiation.sub.find(gen);
  // The substitution must have an intersection with an already deduced value for the same type variable
  if (existing != instantiation.sub.end()) {
    const auto* intersection = gen_sub->intersect(*existing->second);
    if (intersection == nullptr) {
      // The types don't have a common intersection, they cannot coexist.
      return nullptr;
    }
    instantiation.sub[gen] = intersection;
  } else {
    instantiation.sub.try_emplace(gen, gen_sub);
  }

  return instantiation.sub.at(gen);
}

template <typename T> void Overload<T>::dump(llvm::raw_ostream& stream) const {
  stream << fn->ast.location().to_string() << "\t" << fn->name() << "(";
  join_args(fn->ast.args(), T::arg_type, stream);
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

template <typename T> void OverloadSet<T>::dump(llvm::raw_ostream& stream, bool hide_invalid) const {
  stream << T::overload_name(*call) << "(";
  join_args(args, get_val_ty, stream);
  stream << ")\n";
  for (const auto& i_s : overloads) {
    if (hide_invalid && !i_s.viable)
      continue;
    stream << "  ";
    i_s.dump(stream);
    stream << "\n";
  }
};

static auto literal_cast(ast::AST& arg, const ty::Type* target_type) -> ty::Compat {
  if (arg.val_ty() == target_type)
    return {.valid = true}; // Already the correct type

  if (isa<ast::NumberExpr>(arg) && isa<ty::Int>(target_type)) {
    auto& num_arg = cast<ast::NumberExpr>(arg);
    const auto* int_type = cast<ty::Int>(target_type);

    if (int_type->size() == 1)
      return {}; // Can't implicitly cast to Bool

    auto in_range = int_type->in_range(num_arg.val());

    if (in_range)
      return {.valid = true, .conv = {.dereference = false, .kind = ty::Conv::Int}};
  }

  return {};
}

template <typename T> auto parameter_count_matches(const vector<ast::AST*>& args, const T& fn_ast) -> bool;
template <> auto parameter_count_matches(const vector<ast::AST*>& args, const ast::FnDecl& fn_ast) -> bool {
  // Varargs functions may have more arguments that the amount of non-vararg parameters.
  if (args.size() != fn_ast.args().size())
    if (!fn_ast.varargs() || args.size() < fn_ast.args().size())
      return false;
  return true;
}
template <> auto parameter_count_matches(const vector<ast::AST*>& args, const ast::CtorDecl& fn_ast) -> bool {
  return args.size() == fn_ast.args().size();
}

template <typename T> auto OverloadSet<T>::is_valid_overload(Overload<T>& overload) -> bool {
  const auto& fn_ast = overload.fn->ast;

  // The overload is only viable if the amount of arguments matches the amount of parameters.
  if (!parameter_count_matches(args, fn_ast))
    return false;

  overload.compatibilities.reserve(args.size());

  // Determine the type compatibility of each argument individually. The performed conversions are also recorded for
  // each step.
  // As `llvm::zip` only iterates up to the size of the shorter argument, we don't try to determine type
  // compatibility of the "variadic" part of varargs functions. Currently, varargs methods can only be primitives and
  // carry no type information for their variadic part. This will change in the future.
  for (const auto& [param, arg] : llvm::zip_first(fn_ast.args(), args)) {
    const auto* arg_type = arg->val_ty();
    const auto* param_type = T::arg_type(param);

    if (param_type->is_generic()) {
      auto sub = arg_type->determine_generic_substitution(*param_type);

      // No valid substitution found
      if (sub.target == nullptr || sub.replace == nullptr)
        return false;

      // Determined a substitution for a type variable, check it against others (if there are any).
      const auto* new_target = intersect_generics(overload.instantiation, sub.target, sub.replace);
      if (new_target == nullptr)
        return false; // Incompatibility

      param_type = param_type->apply_generic_substitution({sub.target, new_target});

      if (param_type == nullptr)
        return false;
    }

    // Attempt to do a literal cast
    auto compat = literal_cast(*arg, param_type);
    // Couldn't perform a literal cast, try regular casts
    if (!compat.valid)
      compat = arg_type->compatibility(*param_type);

    // Couldn't perform any kind of valid cast: one invalid conversion disqualifies the function entirely
    if (!compat.valid)
      return false;

    // Save the steps needed to perform the conversion
    overload.compatibilities.push_back(compat);
  }

  // Add empty conversions for each argument which maps to a variadic
  while (overload.compatibilities.size() < args.size())
    overload.compatibilities.emplace_back();

  // Must be valid!
  return true;
}

template <typename T> void OverloadSet<T>::determine_valid_overloads() {
  auto& [call_expr, overloads, args] = *this;

  // All `Overload`s are determined to not be viable by default, so determine the ones which actually are
  // TODO: Actually keep track of *why* a type is not viable, for diagnostics.
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

template <typename T> auto Overload<T>::better_candidate_than(Overload other) const -> bool {
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

    // Cannot distinguish between these ones, try the next arguments
  }

  // If we got to here, it means all arguments were identical. Neither overload is better than the other
  return false;
}

template <typename T> auto OverloadSet<T>::best_viable_overload() const -> Overload<T> {
  const Overload<T>* best = nullptr;

  for (const auto& candidate : overloads)
    if (candidate.viable)
      if (best == nullptr || candidate.better_candidate_than(*best))
        best = &candidate;

  if (best == nullptr) {
    string str{};
    llvm::raw_string_ostream ss{str};
    ss << "No viable overload for " << T::overload_name(*call) << " with argument types ";
    join_args(args, get_val_ty, ss);
    throw std::logic_error(str);
  }

  vector<const Overload<T>*> ambiguous;

  for (const auto& candidate : overloads)
    if (candidate.viable && &candidate != best)
      if (!best->better_candidate_than(candidate))
        ambiguous.push_back(&candidate);

  if (ambiguous.empty())
    return *best;

  ambiguous.push_back(best);

  string str{};
  llvm::raw_string_ostream ss{str};
  ss << "Ambigious call for " << T::overload_name(*call) << " with argument types ";
  join_args(args, get_val_ty, ss);
  ss << "\nCouldn't pick between the following overloads:\n";
  for (const auto* i : ambiguous) {
    i->dump(ss);
    ss << "\n";
  }

  throw std::logic_error(str);
}

template struct Overload<Fn>;
template struct Overload<Ctor>;
template struct OverloadSet<Fn>;
template struct OverloadSet<Ctor>;

} // namespace yume::semantic
