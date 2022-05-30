#include <cxxabi.h>
#include <exception>
#include <llvm/Support/Signals.h>
#include <llvm/Support/raw_ostream.h>

namespace yume {
inline auto what(const std::exception_ptr& eptr = std::current_exception()) -> std::string_view {
  if (!eptr) {
    throw std::bad_exception();
  }

  try {
    std::rethrow_exception(eptr);
  } catch (const std::exception& e) {
    return e.what();
  } catch (const std::string& e) {
    return e;
  } catch (const char* e) {
    return e;
  } catch (...) {
    return "[none available]";
  }
}

// `free`ing pointers is really not important when the application has already crashed
inline auto current_exception_name() -> const char* {
  int status{};
  return abi::__cxa_demangle(abi::__cxa_current_exception_type()->name(), nullptr, nullptr, &status);
}

// Performance is really not important when the application has already crashed
inline void stacktrace() {
  auto exception = std::current_exception();
  if (exception != nullptr) {
    using enum llvm::raw_fd_ostream::Colors;
    llvm::errs().changeColor(RED, true) << "Uncaught exception resulted in termination!\n";
    llvm::errs().changeColor(RED) << "  Ultimately caught ";
    llvm::errs().resetColor() << current_exception_name() << "\n";
    llvm::errs().changeColor(RED) << "  Additional info: ";
    llvm::errs().changeColor(YELLOW) << what(exception) << "\n\n";
    llvm::errs().resetColor();
  }
  llvm::sys::PrintStackTrace(llvm::errs());
}
} // namespace yume