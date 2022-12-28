#include "ast/ast.hpp"
#include "compiler/compiler.hpp"
#include "compiler/vals.hpp"
#include "diagnostic/errors.hpp"
#include "diagnostic/visitor/dot_visitor.hpp"
#include "diagnostic/visitor/print_visitor.hpp"
#include "token.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if __has_include("yumec-version.hpp")
#include "yumec-version.hpp"
#else
namespace yume {
static constexpr std::string_view VERSION = "???";
static constexpr std::string_view GIT_SHORTHASH = "???";
} // namespace yume
#endif

using namespace std::string_literals;

enum struct CompilerFlags {
  None = 0,
  NoLink = 1 << 0,
  EmitLLVM = 1 << 1,
  EmitASM = 1 << 2,
  EmitDot = 1 << 3,
  EmitUntypedDot = 1 << 4,
  DumpAST = 1 << 5,
  NoPrelude = 1 << 6,
};

inline auto operator|(CompilerFlags a, CompilerFlags b) -> CompilerFlags {
  return static_cast<CompilerFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline auto operator|=(CompilerFlags& a, CompilerFlags b) -> CompilerFlags { return a = a | b; }

inline auto operator~(CompilerFlags a) -> CompilerFlags { return static_cast<CompilerFlags>(~static_cast<int>(a)); }

inline auto operator&(CompilerFlags a, CompilerFlags b) -> bool {
  return (static_cast<int>(a) & static_cast<int>(b)) != 0;
}

auto lib_dir() -> std::string {
  const char* env_lib_dir = std::getenv("YUME_LIB_DIR");
  if (env_lib_dir == nullptr)
    env_lib_dir = YUME_LIB_DIR;

  return env_lib_dir;
}

auto compile(const std::optional<std::string>& target_triple, std::vector<std::string> src_file_names,
             CompilerFlags flags) -> int {
  if (~flags & CompilerFlags::NoPrelude)
    src_file_names.insert(src_file_names.begin(), lib_dir() + "std.ym");

  std::vector<yume::SourceFile> source_files{};
  source_files.reserve(src_file_names.size());

  {
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> inputs{};
    inputs.reserve(src_file_names.size());

    std::unique_ptr<llvm::raw_ostream> dot_file{};
    std::unique_ptr<yume::diagnostic::DotVisitor> dot_visitor{};
    if (flags & CompilerFlags::EmitUntypedDot) {
      dot_file = yume::open_file("output_untyped.dot");
      dot_visitor = std::make_unique<yume::diagnostic::DotVisitor>(*dot_file);
    }

    for (const auto& i : src_file_names) {
      auto buffer = llvm::MemoryBuffer::getFileOrSTDIN(i);
      if (!buffer)
        throw std::runtime_error("While opening file "s + i + ": " + buffer.getError().message());

      inputs.emplace_back(std::move(buffer.get()));
    }

    for (auto& src_input : inputs) {
      auto src_name = src_input->getBufferIdentifier().str();
      auto src_path =
          src_name == "-" ? std::filesystem::path{} : std::filesystem::canonical(std::filesystem::absolute(src_name));
      auto src_stream = std::stringstream(std::string(src_input->getBufferStart(), src_input->getBufferSize()));
      auto& source = source_files.emplace_back(src_stream, src_path);

      if (flags & CompilerFlags::EmitUntypedDot)
        dot_visitor->visit(*source.program, "");

      if (flags & CompilerFlags::DumpAST) {
        yume::diagnostic::PrintVisitor(llvm::errs(), true).visit(*source.program, "");
        llvm::errs() << "\n";
      }

      auto token_it = source.iterator;
      if (!token_it.at_end()) {
        llvm::outs() << "unconsumed tokens:\n";
        while (!token_it.at_end())
          llvm::outs() << "  " << *token_it++ << "\n";
        llvm::outs() << "\n";
        llvm::outs().flush();

        return 2;
      }
    }
  }

  if (flags & CompilerFlags::DumpAST)
    return 0;

  auto compiler = yume::Compiler{target_triple, std::move(source_files)};
  compiler.run();

  if (flags & CompilerFlags::EmitDot) {
    for (const auto& i : compiler.source_files()) {
      const std::string full_name = "output_"s + i.path.stem().native() + ".dot";
      auto dot = yume::open_file(full_name.c_str());
      auto visitor = yume::diagnostic::DotVisitor{*dot};
      visitor.visit(*i.program, "");
    }
  }

  if (flags & CompilerFlags::EmitLLVM)
    compiler.module()->print(*yume::open_file("output.ll"), nullptr);
  if (flags & CompilerFlags::EmitASM)
    compiler.write_object("output.s", false);
  compiler.write_object("output.o", true);
  llvm::outs().flush();
  if (~flags & CompilerFlags::NoLink)
    std::system("cc output.o -o yume.out");

  return EXIT_SUCCESS;
}

void emit_version() {
  llvm::outs() << "yume version " << yume::VERSION << "-" << yume::GIT_SHORTHASH << "\n";
  llvm::outs() << "lib: " << lib_dir() << "\n";
  llvm::outs() << "build-time LIB_DIR: " YUME_LIB_DIR "\n";
  llvm::outs() << "build-time SRC_DIR: " YUME_SRC_DIR "\n";
}

auto main(int argc, const char* argv[]) -> int {
  auto raw_args = std::span(argv, argc);
  auto args = raw_args.subspan(1); // omit argv 0 (program name)

  llvm::outs().enable_colors(llvm::outs().has_colors());
  llvm::errs().enable_colors(llvm::errs().has_colors());

  auto fatal_error = [&]() -> auto& {
    emit_version();
    llvm::outs() << "\n";
    llvm::outs().changeColor(llvm::raw_ostream::WHITE, true) << raw_args[0];
    llvm::outs().resetColor() << ": ";
    llvm::outs().changeColor(llvm::raw_ostream::RED) << "error";
    llvm::outs().resetColor() << ": ";
    return llvm::outs();
  };

  std::optional<std::string> target_triple = {};
  std::vector<std::string> source_file_names = {};
  bool consuming_target = false;
  bool done_with_flags = false;
  auto flags = CompilerFlags::None;

  for (const auto& arg : args) {
    if (consuming_target) {
      target_triple = arg;
      consuming_target = false;
      continue;
    }
    if (arg == "--version"s) {
      emit_version();
      return EXIT_SUCCESS;
    }
    if (arg == "--target"s) {
      consuming_target = true;
    } else if (arg == "-c"s) {
      flags |= CompilerFlags::NoLink;
    } else if (arg == "--emit-llvm"s) {
      flags |= CompilerFlags::EmitLLVM;
    } else if (arg == "--emit-asm"s) {
      flags |= CompilerFlags::EmitASM;
    } else if (arg == "--emit-dot"s) {
      flags |= CompilerFlags::EmitDot;
    } else if (arg == "--emit-untyped-dot"s) {
      flags |= CompilerFlags::EmitUntypedDot;
    } else if (arg == "--dump-ast"s) {
      flags |= CompilerFlags::DumpAST;
    } else if (arg == "--no-prelude"s) {
      flags |= CompilerFlags::NoPrelude;
    } else if (arg == "--"s) {
      done_with_flags = true;
    } else if (!done_with_flags && std::string(arg).starts_with('-')) {
      fatal_error() << "unknown flag " << arg << "\n";
      return 3;
    } else {
      source_file_names.emplace_back(arg);
    }
  }

  if (source_file_names.empty()) {
    fatal_error() << "provide at least one source file\n";
    return 1;
  }

  std::set_terminate(yume::print_exception);
  llvm::EnablePrettyStackTrace();
  llvm::setBugReportMsg("");
  llvm::sys::AddSignalHandler(yume::backtrace, args.data());

  return compile(target_triple, source_file_names, flags);
}
