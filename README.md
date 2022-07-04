# yume

An experiment in programming language design. Not meant to be used seriously!

Check out the `example/` directory to see what the language looks like. All examples *should* compile, except for those in the `unimplemented` directory

## Usage

The Yume compiler is written in modern C++20 and built using CMake. Requires LLVM to be installed as a library. [See below](#compiler-compatiblity) for compatibility with compilers

```sh
cmake -B build && cmake --build build
# Then try an example!
build/yumec example/collatz.ym
```

Yume compiles to LLVM statically. The compiler outputs the resulting linked binary as `yume.out`.

## Build-time compiler switches

To apply these switches, add them to the cmake command when building

Debug:  
`-DYUME_SANITIZE=1`: add `-fsanitize` flags  
`-DYUME_EMIT_DOT=1`: emit pretty GraphViz .dot files for AST output  
`-DYUME_SPEW=1`: very verbose output (higher numbers mean more output)  
`-DYUME_FORCE_LLVM_VERSION=13`: if the system has multiple versions of LLVM, specify which one to use

## Compiler compatibility

The Yume compiler uses C++20 features which may not be fully implemented in all places.  
**The target environment is LLVM 14, with clang 14 on libstdc++**

- CI tests the compiler using **LLVM versions 13, 14 and 15** (bleeding edge); and using **clang version 13 and 14**, using **libstdc++**.
- Compatibility with libc++ is on a best-effort casis: A few shims exist to add C++ stdlib features still missing from c++ and a build-time flag can be used to force using libc++, provided the system is ready to provide a version of llvm also using libc++.
- Compatibility with gcc is not checked, and left as an excercise to the user

## Contributors

- [Emilia Dreamer](https://gitlab.com/rymiel) - creator and maintainer
