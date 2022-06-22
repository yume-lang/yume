cmake_minimum_required(VERSION 3.23)
project(yume_cpp)

set(CMAKE_CXX_STANDARD 20)

if(YUME_SANITIZE)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -glldb")
	else()
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
	endif()
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -O0 -fsanitize=undefined,vptr")
endif()

if(YUME_SPEW GREATER 3)
        add_definitions(-DYUME_SPEW_DOT_TOKEN_HEADER)
endif()
if(YUME_SPEW GREATER 2)
        add_definitions(-DYUME_SPEW_OVERLOAD_SELECTION)
endif()
if(YUME_SPEW GREATER 1)
        add_definitions(-DYUME_SPEW_LIST_TOKENS -DYUME_SPEW_TYPE_WALKER_STUB)
endif()
if(YUME_SPEW GREATER 0)
        add_definitions(-DYUME_SPEW_CONSUMED_TOKENS)
endif()
if(YUME_EMIT_DOT)
        add_definitions(-DYUME_EMIT_DOT)
endif()

add_definitions("-DYUME_LIB_DIR=\"${CMAKE_SOURCE_DIR}/lib/\"")

find_package(LLVM ${YUME_FORCE_LLVM_VERSION} REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

message(STATUS "Using LLVM include: ${LLVM_INCLUDE_DIRS}")
message(STATUS "Using LLVM library: ${LLVM_LIBRARY_DIRS}")
message(STATUS "Using LLVM tool dir: ${LLVM_TOOLS_BINARY_DIR}")

file(GLOB_RECURSE YUME_SRC CONFIGURE_DEPENDS src/*.cpp src/*.hpp)
add_executable(yumec ${YUME_SRC})

target_compile_options(yumec PRIVATE -W -Wall)
if(YUME_FORCE_LIBCXX)
	target_compile_options(yumec INTERFACE -stdlib=libc++)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lc++abi")
endif()

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs all
        OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "LLVM libs: ${LLVM_LIBS}")

target_link_libraries(yumec ${LLVM_LIBS})
