#pragma once

namespace yume {
enum struct Qualifier {
  Ptr,   ///< `ptr`
  Slice, ///< `[]`
  Mut,   ///< `mut`
};

enum struct PtrLikeQualifier {
  Ptr = static_cast<int>(Qualifier::Ptr),
  Slice = static_cast<int>(Qualifier::Slice),
  Q_END
};
} // namespace yume
