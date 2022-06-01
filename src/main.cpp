#include "ast.hpp"
#include "compiler.hpp"
#include "errors.hpp"
#include "token.hpp"
#include <fstream>
#include <iostream>


auto main(int argc, const char* argv[]) -> int {
  auto args = std::span(argv, argc);
  std::unique_ptr<std::istream> file_stream{};
  if (argc > 1) {
    file_stream = std::make_unique<std::ifstream>(args[1]);
  }

  std::set_terminate(yume::stacktrace);

  {
    std::vector<std::pair<std::istream*, std::string>> inputs{};
    std::vector<std::unique_ptr<std::istream>> file_streams{};
    auto dot = yume::open_file("output.dot");
    auto visitor = yume::DotVisitor{*dot};

    for (auto& i : src_file_names) {
      if (i == "-") {
        inputs.emplace_back(&std::cin, "<source>");
        continue;
      }
      auto& f = file_streams.emplace_back(std::make_unique<std::ifstream>(i));
      inputs.emplace_back(f.get(), std::string(i));
    }

    for (auto& [src_stream, src_name] : inputs) {
      auto& source = source_files.emplace_back(*src_stream, src_name);
      visitor.Visitor::visit(*source.m_program);

      auto token_it = source.m_iterator;
      if (!token_it.at_end()) {
        std::cout << "unconsumed tokens:\n";
        while (!token_it.at_end()) {
          std::cout << "  " << *token_it++ << "\n";
        }
        std::cout << "\n";
        std::cout.flush();

        exit(2);
      }
    }
  }

  auto compiler = yume::Compiler{std::move(source_files)};
  compiler.module()->print(llvm::outs(), nullptr);
  compiler.module()->print(*yume::open_file("output.ll"), nullptr);
  compiler.write_object("output.s", false);
  compiler.write_object("output.o", true);

  return EXIT_SUCCESS;
}
