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

  auto source = yume::SourceFile(argc > 1 ? *file_stream : std::cin, argc > 1 ? args[1] : "<source>");
  {
    auto dot = yume::open_file("output.dot");
    auto visitor = yume::DotVisitor{*dot};
    visitor.Visitor::visit(*source.m_program);
  }

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

  auto compiler = yume::Compiler{std::move(source)};
  compiler.module()->print(llvm::outs(), nullptr);
  compiler.module()->print(*yume::open_file("output.ll"), nullptr);
  compiler.write_object("output.s", false);
  compiler.write_object("output.o", true);
  //  } catch (std::exception& exc) {
  //    std::cerr << exc.what() << "\n";
  //    exit(1);
  //  }
  return EXIT_SUCCESS;
}
