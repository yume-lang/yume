# yume

An experiment in programming language design. Not meant to be used seriously!

Check out the `examples/` directory to see what the language looks like. All examples *should* compile!

## Usage

The Yume compiler requires [Crystal](https://github.com/crystal-lang/crystal) to run.

`crystal run src/yume.cr -- example/test.ym`

Yume compiles to LLVM statically. The resulting binary will be in `out/yume.out`

## Compiler switches

To apply these switches, add them in the `crystal run` command, before the `--`

Optimize the resulting LLVM IR: `-Doptimize`

Compiler debug switches: `-Ddebug_ast`, `-Ddebug_parse`, `-Ddebug_lex`, `-Ddebug_overload`

## Contributors

- [Emilia Dreamer](https://gitlab.com/rymiel) - creator and maintainer
