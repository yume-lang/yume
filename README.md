# yume

An experiment in programming language design. Not meant to be used seriously!

## Usage

The Yume compiler requires [Crystal](https://github.com/crystal-lang/crystal) to run.

`crystal run src/yume.cr -- example/test.ym`

Yume compiles to LLVM statically. The resulting binary will be in `out/yume.out`

## Development

Compiler debug switches: `-Ddebug_ast`, `-Ddebug_parse`, `-Ddebug_lex`

(Add these in the crystal run command, before the `--`)

## Contributors

- [Emilia Dreamer](https://gitlab.com/rymiel) - creator and maintainer
