#pragma once

namespace yume {
enum struct Qualifier {
  Ptr,   ///< `ptr`
  Slice, ///< `[]`
  Mut,   ///< `mut`
  Q_END  /// Used for the amount of qualifiers.
};
}
