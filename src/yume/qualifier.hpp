#pragma once

namespace yume {
enum struct Qualifier {
  Ptr, ///< `ptr`
  Mut, ///< `mut`
};

enum struct PtrLikeQualifier {
  Ptr = static_cast<int>(Qualifier::Ptr), ///< `ptr`
  Q_END
};
} // namespace yume
