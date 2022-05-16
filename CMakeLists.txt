cmake_minimum_required(VERSION 3.23)
project(yume_cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_DEBUG "-glldb -fno-omit-frame-pointer -DYUME_SPEW_CONSUMED_TOKENS -O0")

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(yumec src/main.cpp src/compiler.hpp src/util.hpp src/compiler.cpp src/token.cpp src/token.hpp src/ast.cpp src/ast.hpp src/visitor.hpp src/visitor.cpp)

execute_process(COMMAND llvm-config --libs all
        OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "LLVM libs: ${LLVM_LIBS}")

target_link_libraries(yumec ${LLVM_LIBS})