#pragma once

namespace yume {
enum struct Qualifier {
  Ptr,    ///< `ptr`
  Mut,    ///< `mut`
  Ref,    ///< `ref`
  Type,   ///< `type`
  Opaque, ///< Opaque self types
};
} // namespace yume
