cmake_minimum_required(VERSION 3.23)
project(yume_cpp)

file(GLOB_RECURSE YUME_SRC CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
add_executable(yumec ${YUME_SRC})

set_property(TARGET yumec PROPERTY CXX_STANDARD 20)
set_property(TARGET yumec PROPERTY EXPORT_COMPILE_COMMANDS TRUE)

if(YUME_SANITIZE)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(yumec PRIVATE "$<$<CONFIG:DEBUG>:-g;-glldb>")
  else()
    target_compile_options(yumec PRIVATE "$<$<CONFIG:DEBUG>:-g3>")
  endif()

  target_compile_options(yumec PRIVATE "$<$<CONFIG:DEBUG>:-fno-omit-frame-pointer;-O0;-fsanitize=undefined,vptr>")
  target_link_options(yumec PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=undefined,vptr>")
endif()

if(YUME_SPEW GREATER 2)
  target_compile_definitions(yumec PRIVATE YUME_SPEW_OVERLOAD_SELECTION)
endif()

if(YUME_SPEW GREATER 1)
  target_compile_definitions(yumec PRIVATE YUME_SPEW_LIST_TOKENS)
endif()

if(YUME_SPEW GREATER 0)
  target_compile_definitions(yumec PRIVATE YUME_SPEW_CONSUMED_TOKENS)
endif()

if(YUME_EMIT_DOT)
  target_compile_definitions(yumec PRIVATE YUME_EMIT_DOT)
endif()

target_compile_definitions(yumec PRIVATE "YUME_LIB_DIR=\"${CMAKE_SOURCE_DIR}/lib/\"")

find_package(LLVM ${YUME_FORCE_LLVM_VERSION} REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

target_include_directories(yumec PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_directories(yumec PUBLIC ${LLVM_LIBRARY_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(yumec PRIVATE ${LLVM_DEFINITIONS_LIST})

message(STATUS "Using LLVM include: ${LLVM_INCLUDE_DIRS}")
message(STATUS "Using LLVM library: ${LLVM_LIBRARY_DIRS}")
message(STATUS "Using LLVM tool dir: ${LLVM_TOOLS_BINARY_DIR}")

target_compile_options(yumec PRIVATE -W -Wall)

if(YUME_FORCE_LIBCXX)
  target_compile_options(yumec INTERFACE -stdlib=libc++)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lc++abi")
endif()

if(YUME_IWYU)
  find_program(iwyu_path NAMES include-what-you-use iwyu REQUIRED)
  set_property(TARGET yumec PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()

target_include_directories(yumec BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs all
  OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "LLVM libs: ${LLVM_LIBS}")

target_link_libraries(yumec ${LLVM_LIBS})

find_package(Catch2 3)
message(STATUS "Catch2: ${Catch2_FOUND}")

if(Catch2_FOUND)
  file(GLOB_RECURSE YUME_TEST CONFIGURE_DEPENDS test/*.cpp)
  add_executable(yume_test ${YUME_TEST})
  set_target_properties(yume_test PROPERTIES EXCLUDE_FROM_ALL TRUE)

  target_include_directories(yume_test BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(yume_test PRIVATE Catch2::Catch2WithMain)

  include(CTest)
  include(Catch)
  catch_discover_tests(yume_test)
endif()
