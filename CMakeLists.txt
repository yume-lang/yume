cmake_minimum_required(VERSION 3.23)
project(yume_cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -W -Wall")

if(YUME_SANITIZE)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -glldb -fno-omit-frame-pointer -O0 -fsanitize=undefined,vptr")
        # set(CMAKE_CXX_FLAGS_DEBUG "-g3 -fno-omit-frame-pointer -O0")
endif()

if(YUME_SPEW)
        add_definitions(-DYUME_SPEW_CONSUMED_TOKENS -DYUME_SPEW_DOT_TOKEN_HEADER -DYUME_SPEW_LIST_TOKENS)
endif()

add_definitions("-DYUME_LIB_DIR=\"${CMAKE_SOURCE_DIR}/lib/\"")

find_package(LLVM ${YUME_FORCE_LLVM_VERSION} REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(yumec src/main.cpp src/compiler.hpp src/util.hpp src/compiler.cpp src/token.cpp src/token.hpp src/ast.cpp src/ast.hpp src/visitor.hpp src/visitor.cpp src/type.cpp src/type.hpp src/errors.hpp)

execute_process(COMMAND llvm-config --libs all
        OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "LLVM libs: ${LLVM_LIBS}")

target_link_libraries(yumec ${LLVM_LIBS})
