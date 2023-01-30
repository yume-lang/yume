#pragma once

#include "ast/ast.hpp"
#include "qualifier.hpp"
#include "ty/substitution.hpp"
#include "ty/type_base.hpp"
#include "util.hpp"
#include <array>
#include <cstdint>
#include <llvm/IR/Type.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class StructType;
class FunctionType;
} // namespace llvm
namespace yume {
class Compiler;
struct Instantiation;
struct Struct;
struct GenericTypeReplacements;
namespace ast {
struct TypeName;
class Type;
} // namespace ast
} // namespace yume

namespace yume::ty {
class Type;

/// A built-in integral type, such as I32 or Bool.
class Int final : public BaseType {
  int m_size;
  bool m_signed;

public:
  Int(string name, int size, bool is_signed) : BaseType(K_Int, move(name)), m_size(size), m_signed(is_signed) {}
  [[nodiscard]] auto size() const -> int { return m_size; }
  [[nodiscard]] auto is_signed() const -> bool { return m_signed; }
  [[nodiscard]] auto name() const -> string override { return base_name(); };
  [[nodiscard]] auto in_range(int64_t num) const -> bool;
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Int; }
};

/// The null type, Nil.
class Nil final : public BaseType {
public:
  constexpr static const auto nil_name = "Nil"; // TODO(rymiel): Magic value?

  Nil() : BaseType(K_Nil, nil_name) {}
  [[nodiscard]] auto name() const -> string override { return nil_name; };
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Nil; }
};

/// A "qualified" type, with a stackable qualifier, \e i.e. `ptr`.
class Ptr : public BaseType {
  // TODO(rymiel): why are these here?
  friend BaseType;

private:
  Type m_base;
  // TODO(rymiel): redundant?
  Qualifier m_qual;

public:
  Ptr(string name, Type base, Qualifier qual) : BaseType(K_Ptr, move(name)), m_base(base), m_qual(qual) {}
  [[nodiscard]] auto pointee() const -> Type { return m_base; }
  [[nodiscard]] auto qualifier() const -> Qualifier { return m_qual; }
  [[nodiscard]] auto has_qualifier(Qualifier qual) const -> bool { return m_qual == qual; }
  [[nodiscard]] auto name() const -> string override;
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Ptr; }
};

/// An user-defined struct type with associated fields.
class Struct final : public BaseType {
  vector<ast::TypeName*> m_fields;
  nullable<const Substitutions*> m_subs;
  // TODO(rymiel): This could probably be removed, it is currently only used to redirect instantiations to the primary
  // definition, if `get_or_create_instantiation` would be called on something which is already an instantiation.
  // However, I don't think that ever even happens, and doing so would probably mean something is wrong. Furthermore,
  // the primary generic could probably be lookup up via `m_decl`, if really needed.
  nullable<const Struct*> m_parent{};
  nonnull<yume::Struct*> m_decl{};

  // TODO(rymiel): There isn't actually anything different about these instances, merely the different address for
  // making the pointers not equal, and the m_subs fields, which isn't really used for much.
  // All the differences, as in, the actual substituted types of the fields, are swapped out by the compiler in a
  // convoluted chain going from `get_or_create_instantiation` -> TypeWalker's decl_queue -> Compiler's `create_struct`.
  // Considering that `get_or_create_instantiation` sets the new instantiation up to follow this chain, but doesn't
  // guarantee that it actually happens, is probably problematic.
  // For this case, since we have access to `m_decl`, we can use it to store instantiations, which would happen
  // anyway, however, this would only work for struct types. Function types need a similar treatment, as they are also
  // "structural", as in, containing multiple fields. Since function types have no decl to "call back to", all of its
  // instantiation logic, including substitution, needs to happen in-place.
  // The difference with Function and Struct, is that Struct uses ast::TypeName, whereas Function uses ty::Type, which
  // are a lot easier to substitute
  mutable std::unordered_map<Substitutions, unique_ptr<Struct>> m_instantiations{};
  mutable llvm::Type* m_memo{};

  // TODO(rymiel): why are these here?
  friend Compiler;
  friend BaseType;
  friend Type;

public:
  Struct(string name, vector<ast::TypeName*> fields, nonnull<yume::Struct*> decl, nullable<const Substitutions*> subs)
      : BaseType(K_Struct, move(name)), m_fields(move(fields)), m_subs(subs), m_decl(decl) {}
  [[nodiscard]] auto fields() const -> const auto& { return m_fields; }
  [[nodiscard]] auto fields() -> auto& { return m_fields; }
  [[nodiscard]] auto subs() const -> const auto* { return m_subs; }
  [[nodiscard]] auto subs() -> auto* { return m_subs; }
  [[nodiscard]] auto decl() const -> nonnull<yume::Struct*> { return m_decl; }
  [[nodiscard]] auto is_interface() const -> bool;
  [[nodiscard]] auto implements() const -> const ast::OptionalType&;
  [[nodiscard]] auto name() const -> string override;

  [[nodiscard]] auto memo() const -> auto* { return m_memo; }
  void memo(Compiler& /* key */, llvm::Type* memo) const { m_memo = memo; }

  [[nodiscard]] auto get_or_create_instantiation(Substitutions sub) const -> const Struct&;

  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Struct; }
};

/// A function pointer type
class Function : public BaseType {
  vector<Type> m_args;
  optional<Type> m_ret;
  vector<Type> m_closure;
  bool m_fn_ptr;
  bool m_c_varargs;

  nullable<const Function*> m_parent{};
  mutable std::unordered_map<Substitutions, unique_ptr<Function>> m_instantiations{};

  mutable llvm::FunctionType* m_fn_memo{};
  mutable llvm::StructType* m_closure_memo{};
  mutable llvm::Type* m_memo{};

  // TODO(rymiel): why are these here?
  friend Compiler;
  friend BaseType;
  friend Type;

public:
  Function(string name, vector<Type> args, optional<Type> ret, vector<Type> closure, bool fn_ptr,
           bool c_varargs = false)
      : BaseType(K_Function, move(name)), m_args(move(args)), m_ret(ret), m_closure(move(closure)), m_fn_ptr(fn_ptr),
        m_c_varargs(c_varargs) {}
  [[nodiscard]] auto args() const -> const auto& { return m_args; }
  [[nodiscard]] auto args() -> auto& { return m_args; }
  [[nodiscard]] auto closure() const -> const auto& { return m_closure; }
  [[nodiscard]] auto closure() -> auto& { return m_closure; }
  [[nodiscard]] auto ret() const -> const auto& { return m_ret; }
  [[nodiscard]] auto is_fn_ptr() const { return m_fn_ptr; }
  [[nodiscard]] auto is_c_varargs() const { return m_c_varargs; }
  [[nodiscard]] auto name() const -> string override;

  [[nodiscard]] auto fn_memo() const -> auto* { return m_fn_memo; }
  [[nodiscard]] auto closure_memo() const -> auto* { return m_closure_memo; }
  [[nodiscard]] auto memo() const -> auto* { return m_memo; }

  void fn_memo(Compiler& /* key */, llvm::FunctionType* memo) const { m_fn_memo = memo; }
  void closure_memo(Compiler& /* key */, llvm::StructType* memo) const { m_closure_memo = memo; }
  void memo(Compiler& /* key */, llvm::Type* memo) const { m_memo = memo; }

  [[nodiscard]] auto get_or_create_instantiation(Substitutions sub) const -> const Function&;

  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Function; }
};

/// An unsubstituted generic type variable, usually something like `T`.
///
/// Note that two different functions with the same name for a type variable use two different instances of `Generic`.
class Generic final : public BaseType {
public:
  explicit Generic(string name) : BaseType(K_Generic, move(name)) {}
  [[nodiscard]] auto name() const -> string override { return base_name(); };
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Generic; }
};

/// The "self" type of abstract or overriding functions. An extra layer of indirection is introduced for type erasure
class OpaqueSelf final : public BaseType {
private:
  const BaseType* m_indirect;

public:
  explicit OpaqueSelf(const BaseType* indirect) : BaseType(K_OpaqueSelf, indirect->name()), m_indirect(indirect) {}
  [[nodiscard]] auto name() const -> string override { return base_name() + " opaque"; };
  [[nodiscard]] auto indirect() const -> const BaseType* { return m_indirect; };
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_OpaqueSelf; }
};

/// A metatype, that is, a type referring to a type.
class Meta final : public BaseType {
private:
  const BaseType* m_indirect;

public:
  explicit Meta(const BaseType* indirect) : BaseType(K_Meta, indirect->name()), m_indirect(indirect) {}
  [[nodiscard]] auto name() const -> string override { return base_name() + " type"; };
  [[nodiscard]] auto indirect() const -> const BaseType* { return m_indirect; };
  static auto classof(const BaseType* a) -> bool { return a->kind() == K_Meta; }
};
} // namespace yume::ty
