# yume

An experiment in programming language design. Not meant to be used seriously!

Check out the `examples/` directory to see what the language looks like. ~~All examples *should* compile!~~ (Currently undergoing refactoring)

## Usage

The Yume compiler is written in C++20 and built using CMake. Requires libllvm and llvm-config to be installed.
Only tested with libllvm 13 and Clang 13.

```sh
mkdir build
cd build
cmake ..
make
# Then try an example!
./yumec ../examples/collatz.ym
```

Yume compiles to LLVM statically. The compiler outputs an unlinked binary as `output.o`, which must be linked, for example with `clang -static output.o`, then run `a.out`. This will change in the future

## Compiler switches

To apply these switches, add them to the cmake command when building

Debug:  
`-DYUME_SANITIZE=1`: add `-fsanitize` clang flags  
`-DYUME_SPEW=1`: very verbose output

## Contributors

- [Emilia Dreamer](https://gitlab.com/rymiel) - creator and maintainer
