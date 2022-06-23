#include "ast/ast.hpp"
#include "compiler/compiler.hpp"
#include "compiler/vals.hpp"
#include "diagnostic/dot_visitor.hpp"
#include "diagnostic/errors.hpp"
#include "token.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// this should be provided by cmake
#ifndef YUME_LIB_DIR
#define YUME_LIB_DIR "./lib/"
#endif

auto main(int argc, const char* argv[]) -> int {
  auto args = std::span(argv, argc);
  std::vector<std::string> src_file_names{};
  src_file_names.reserve(argc + 1);
  src_file_names.push_back(std::string(YUME_LIB_DIR) + "std.ym");
  std::copy(args.begin() + 1, args.end(), std::back_inserter(src_file_names));

  std::vector<yume::SourceFile> source_files{};
  source_files.reserve(src_file_names.size());

  std::set_terminate(yume::stacktrace);
  llvm::EnablePrettyStackTrace();
  llvm::setBugReportMsg("");
  llvm::sys::PrintStackTraceOnErrorSignal(args[0]);

  {
    std::vector<std::unique_ptr<llvm::MemoryBuffer>> inputs{};
    inputs.reserve(src_file_names.size());
#ifdef YUME_EMIT_DOT
    auto dot = yume::open_file("output_untyped.dot");
    auto visitor = yume::diagnostic::DotVisitor{*dot};
#endif

    for (const auto& i : src_file_names)
      inputs.emplace_back(std::move(llvm::MemoryBuffer::getFileOrSTDIN(i).get()));

    for (auto& src_input : inputs) {
      auto src_name = src_input->getBufferIdentifier().str();
      auto src_stream = std::stringstream(std::string(src_input->getBufferStart(), src_input->getBufferSize()));
      auto& source = source_files.emplace_back(src_stream, src_name);
#ifdef YUME_EMIT_DOT
      visitor.visit(*source.m_program, nullptr);
#endif

      auto token_it = source.m_iterator;
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
  compiler.module()->print(*yume::open_file("output.ll"), nullptr);
  compiler.write_object("output.s", false);
  compiler.write_object("output.o", true);
  llvm::outs().flush();
  std::system("cc output.o -o yume.out");

#ifdef YUME_EMIT_DOT
  for (const auto& i : compiler.source_files()) {
    std::string full_name = "output_"s + std::string(yume::stem(i.m_name)) + ".dot";
    auto dot = yume::open_file(full_name.c_str());
    auto visitor = yume::diagnostic::DotVisitor{*dot};
    visitor.visit(*i.m_program, nullptr);
  }
#endif

  return EXIT_SUCCESS;
}
