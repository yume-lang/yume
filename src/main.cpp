#include "ast.hpp"
#include "compiler.hpp"
#include "token.hpp"
#include "errors.hpp"
#include <fstream>
#include <iostream>

auto main(int argc, const char* argv[]) -> int {
  auto args = std::span(argv, argc);
  std::unique_ptr<std::istream> file_stream{};
  if (argc > 1) {
    file_stream = std::make_unique<std::ifstream>(args[1]);
  }

  std::set_terminate(yume::stacktrace);

  auto tokens = yume::tokenize(argc > 1 ? *file_stream : std::cin);
  std::cout << "tokens:\n";
  for (auto& i : tokens) {
    std::cout << "  " << i << "\n";
  }
  std::cout << "\n";
  std::cout.flush();
  auto token_it = yume::ast::TokenIterator{tokens.begin(), tokens.end()};
  std::unique_ptr<llvm::raw_ostream> dot = yume::open_file("output.dot");
  auto visitor = yume::DotVisitor{*dot};
  auto program = yume::ast::Program::parse(token_it);
  visitor.Visitor::visit(*program);
  visitor.finalize();

  if (!token_it.end()) {
    std::cout << "unconsumed tokens:\n";
    while (!token_it.end()) {
      std::cout << "  " << *token_it++ << "\n";
    }
    std::cout << "\n";
    std::cout.flush();

    exit(2);
  }

  //   try {
  auto compiler = yume::Compiler{move(program)};
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
