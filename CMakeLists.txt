cmake_minimum_required(VERSION 3.23)
project(Yume VERSION 0.1.0)

option(YUME_SANITIZE "Enable sanitization flags (-fsanitize)" FALSE)
option(YUME_SPEW "Verbose output (higher numbers mean more)" 0)
option(YUME_IWYU "Check the project with include-what-you-use" FALSE)
option(YUME_COV "Coverage information" FALSE)

find_package(Git)
execute_process(
    COMMAND           ${GIT_EXECUTABLE} describe --always --dirty --match=""
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE   YUME_GIT_SHORTHASH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/yumec-version.hpp.in"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/yumec-version.hpp"
  @ONLY)

file(GLOB_RECURSE YUME_SRC CONFIGURE_DEPENDS src/yume/*.cpp src/yume/*.hpp)
add_library(yume ${YUME_SRC})

set_property(TARGET yume PROPERTY CXX_STANDARD 20)
set_property(TARGET yume PROPERTY EXPORT_COMPILE_COMMANDS TRUE)

if(YUME_SANITIZE)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(yume PUBLIC "$<$<CONFIG:DEBUG>:-g;-glldb>")
  else()
    target_compile_options(yume PUBLIC "$<$<CONFIG:DEBUG>:-g3>")
  endif()

  target_compile_options(yume PUBLIC "$<$<CONFIG:DEBUG>:-fno-omit-frame-pointer;-O0;-fsanitize=address,undefined,vptr>")
  target_link_options(yume PUBLIC "$<$<CONFIG:DEBUG>:-fsanitize=address,undefined,vptr>")
endif()

if(YUME_COV)
  target_compile_options(yume PUBLIC "-fprofile-instr-generate;-fcoverage-mapping")
  target_link_options(yume PUBLIC "-fprofile-instr-generate;-fcoverage-mapping")
endif()
if(YUME_SPEW GREATER 2)
  target_compile_definitions(yume PUBLIC YUME_SPEW_LIST_TOKENS)
endif()

if(YUME_SPEW GREATER 1)
  target_compile_definitions(yume PUBLIC YUME_SPEW_CONSUMED_TOKENS)
endif()

if(YUME_SPEW GREATER 0)
  target_compile_definitions(yume PUBLIC YUME_SPEW_OVERLOAD_SELECTION)
endif()

if(YUME_EMIT_DOT)
  target_compile_definitions(yume PUBLIC YUME_EMIT_DOT)
endif()

target_compile_definitions(yume PUBLIC "YUME_LIB_DIR=\"${CMAKE_SOURCE_DIR}/lib/\"")
target_compile_definitions(yume PUBLIC "YUME_SRC_DIR=\"${CMAKE_SOURCE_DIR}/src/\"")

find_package(LLVM ${YUME_FORCE_LLVM_VERSION} REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

target_include_directories(yume SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_directories(yume PUBLIC ${LLVM_LIBRARY_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(yume PUBLIC ${LLVM_DEFINITIONS_LIST})

message(STATUS "Using LLVM include: ${LLVM_INCLUDE_DIRS}")
message(STATUS "Using LLVM library: ${LLVM_LIBRARY_DIRS}")
message(STATUS "Using LLVM tool dir: ${LLVM_TOOLS_BINARY_DIR}")

target_compile_options(yume PUBLIC -Wno-unknown-warning-option -W -Wall -Wimplicit-fallthrough -Wpedantic -Wno-gnu-zero-variadic-macro-arguments -Wno-unqualified-std-cast-call)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wunused_template SUPPORTS_W_UNUSED_TEMPLATE)
if(SUPPORTS_W_UNUSED_TEMPLATE)
  target_compiler_options(yume PUBLIC -Wunused-template)
endif()

if(YUME_FORCE_LIBCXX)
  target_compile_options(yume PUBLIC -stdlib=libc++)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lc++abi")
endif()

if(YUME_IWYU)
  find_program(iwyu_path NAMES include-what-you-use iwyu REQUIRED)
  set_property(TARGET yume PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()

target_include_directories(yume BEFORE PUBLIC src/yume)

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs all
  OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "LLVM libs: ${LLVM_LIBS}")

target_link_libraries(yume ${LLVM_LIBS})

add_executable(yumec src/yumec.cpp)

set_property(TARGET yumec PROPERTY CXX_STANDARD 20)
set_property(TARGET yumec PROPERTY EXPORT_COMPILE_COMMANDS TRUE)
target_link_libraries(yumec PRIVATE yume)

if(BUILD_TESTING)
  enable_testing()
  find_package(Catch2 3)

  file(GLOB_RECURSE YUME_TEST CONFIGURE_DEPENDS test/*.cpp)
  add_executable(yume_test ${YUME_TEST})
  set_property(TARGET yume_test PROPERTY CXX_STANDARD 20)
  set_target_properties(Catch2::Catch2 PROPERTIES
    INTERFACE_COMPILE_OPTIONS ""
  )
  set_target_properties(Catch2::Catch2WithMain PROPERTIES
    INTERFACE_COMPILE_OPTIONS ""
  )

  target_link_libraries(yume_test PRIVATE Catch2::Catch2WithMain)
  target_link_libraries(yume_test PRIVATE yume)

  # include(CTest)
  include(Catch)
  catch_discover_tests(yume_test)
endif()
