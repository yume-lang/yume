# yume

An experiment in programming language design. Not meant to be used seriously!

Check out the `example/` directory to see what the language looks like. All examples *should* compile, except for those in the `unimplemented` directory

## Usage

The Yume compiler is written in C++20 and built using CMake. Requires libllvm and llvm-config to be installed.
Only tested with libllvm 13 & 14 and Clang 13 & 14.

```sh
cmake -B build && cmake --build build
# Then try an example!
build/yumec example/collatz.ym
```

Yume compiles to LLVM statically. The compiler outputs the resulting linked binary as `yume.out`.

## Compiler switches

To apply these switches, add them to the cmake command when building

Debug:  
`-DYUME_SANITIZE=1`: add `-fsanitize` flags  
`-DYUME_EMIT_DOT=1`: emit pretty GraphViz .dot files for AST output  
`-DYUME_SPEW=1`: very verbose output (higher numbers mean more output)  
`-DYUME_FORCE_LLVM_VERSION=13`: if the system has multiple versions of LLVM, specify which one to use

## Contributors

- [Emilia Dreamer](https://gitlab.com/rymiel) - creator and maintainer
