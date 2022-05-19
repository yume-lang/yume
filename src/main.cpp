#include "ast.hpp"
#include "compiler.hpp"
#include "token.hpp"
#include <iostream>
#include <fstream>

auto main(int argc, const char** argv) -> int {
  std::unique_ptr<std::istream> file_stream{};
  if (argc > 1) {
    file_stream = std::make_unique<std::ifstream>(argv[1]);
  }
      
  auto tokens = yume::tokenize(argc > 1 ? *file_stream : std::cin);
  std::cout << "tokens:\n";
  for (auto& i : tokens) {
    std::cout << "  " << i << "\n";
  }
  std::cout << "\n";
  std::cout.flush();
  auto token_it = yume::ast::TokenIterator{tokens.begin(), tokens.end()};
  try {
    std::unique_ptr<llvm::raw_ostream> dot = yume::open_file("output.dot");
    auto visitor = yume::DotVisitor{*dot};
    auto program = yume::ast::Program::parse(token_it);
    visitor.Visitor::visit(*program);
    visitor.finalize();
  } catch (std::exception& exc) {
    std::cout.flush();
    std::cerr << exc.what() << "\n";
    std::cerr.flush();
  }

  if (!token_it.end()) {
    std::cout << "unconsumed tokens:\n";
    while (!token_it.end()) {
      std::cout << "  " << *token_it++ << "\n";
    }
    std::cout << "\n";
    std::cout.flush();

    exit(2);
  }

  try {
    auto compiler = yume::Compiler{};
    compiler.add_main();
    compiler.module()->print(llvm::outs(), nullptr);
    compiler.module()->print(*yume::open_file("output.ll"), nullptr);
    compiler.write_object("output.s", false);
    compiler.write_object("output.o", true);
  } catch (std::exception& exc) {
    std::cerr << exc.what() << "\n";
    exit(1);
  }
  return EXIT_SUCCESS;
}
