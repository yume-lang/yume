# yume

An experiment in programming language design. Not meant to be used seriously!

Check out the `example/` directory to see what the language looks like. ~~All examples *should* compile!~~ (Currently undergoing refactoring)

## Usage

The Yume compiler is written in C++20 and built using CMake. Requires libllvm and llvm-config to be installed.
Only tested with libllvm 13 & 14 and Clang 13 & 14.

```sh
mkdir build
cd build
cmake .. && make
# Then try an example!
./yumec ../example/collatz.ym
```

Yume compiles to LLVM statically. The compiler outputs the resulting linked binary as `yume.out`.

## Compiler switches

To apply these switches, add them to the cmake command when building

Debug:
`-DYUME_SANITIZE=1`: add `-fsanitize` flags
`-DYUME_SPEW=1`: very verbose output (higher numbers mean more output)
`-DYUME_FORCE_LLVM_VERSION=13`: if the system has multiple versions of LLVM, specify which one to use

## Contributors

- [Emilia Dreamer](https://gitlab.com/rymiel) - creator and maintainer
