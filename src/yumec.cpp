#include "ast/ast.hpp"
#include "compiler/compiler.hpp"
#include "compiler/vals.hpp"
#include "diagnostic/errors.hpp"
#include "diagnostic/visitor/dot_visitor.hpp"
#include "token.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstdlib>
#include <exception>
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

auto compile(std::span<const char*> args) -> int {
  std::vector<std::string> src_file_names{};
  src_file_names.reserve(args.size());
  src_file_names.push_back(std::string(YUME_LIB_DIR) + "std.ym");
  std::copy(args.begin(), args.end(), std::back_inserter(src_file_names));

  std::vector<yume::SourceFile> source_files{};
  source_files.reserve(src_file_names.size());

  {
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> inputs{};
    inputs.reserve(src_file_names.size());
#ifdef YUME_EMIT_DOT
    auto dot = yume::open_file("output_untyped.dot");
    auto visitor = yume::diagnostic::DotVisitor{*dot};
#endif

    for (const auto& i : src_file_names) {
      auto buffer = llvm::MemoryBuffer::getFileOrSTDIN(i);
      if (!buffer)
        throw std::runtime_error("While opening file "s + i + ": " + buffer.getError().message());

      inputs.emplace_back(std::move(buffer.get()));
    }

    for (auto& src_input : inputs) {
      auto src_name = src_input->getBufferIdentifier().str();
      auto src_stream = std::stringstream(std::string(src_input->getBufferStart(), src_input->getBufferSize()));
      auto& source = source_files.emplace_back(src_stream, src_name);
#ifdef YUME_EMIT_DOT
      visitor.visit(*source.program, nullptr);
#endif

      auto token_it = source.iterator;
      if (!token_it.at_end()) {
        llvm::outs() << "unconsumed tokens:\n";
        while (!token_it.at_end()) {
          llvm::outs() << "  " << *token_it++ << "\n";
        }
        llvm::outs() << "\n";
        llvm::outs().flush();

        exit(2);
      }
    }
  }

  auto compiler = yume::Compiler{std::move(source_files)};
  compiler.run();

#ifdef YUME_EMIT_DOT
  for (const auto& i : compiler.source_files()) {
    std::string full_name = "output_"s + std::string(yume::stem(i.name)) + ".dot";
    auto dot = yume::open_file(full_name.c_str());
    auto visitor = yume::diagnostic::DotVisitor{*dot};
    visitor.visit(*i.program, nullptr);
  }
#endif

  compiler.module()->print(*yume::open_file("output.ll"), nullptr);
  compiler.write_object("output.s", false);
  compiler.write_object("output.o", true);
  llvm::outs().flush();
  std::system("cc output.o -o yume.out");

  return EXIT_SUCCESS;
}

void emit_version() {
  llvm::outs() << "yume version " << yume::VERSION << "-" << yume::GIT_SHORTHASH << "\n";
  llvm::outs() << "LIB_DIR: " YUME_LIB_DIR "\n";
  llvm::outs() << "SRC_DIR: " YUME_SRC_DIR "\n";
}

auto main(int argc, const char* argv[]) -> int {
  auto raw_args = std::span(argv, argc);
  auto args = raw_args.subspan(1); // omit argv 0 (program name)

  for (const auto& arg : args) {
    if (arg == "--version"s) {
      emit_version();
      return EXIT_SUCCESS;
    }
  }

  if (args.empty()) {
    emit_version();
    llvm::outs() << "\n";
    llvm::outs() << raw_args[0] << ": error: provide at least one source file\n";

    return EXIT_FAILURE;
  }

  std::set_terminate(yume::print_exception);
  llvm::EnablePrettyStackTrace();
  llvm::setBugReportMsg("");
  llvm::sys::AddSignalHandler(yume::backtrace, args.data());

  return compile(args);
}
