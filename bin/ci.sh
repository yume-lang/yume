#!/usr/bin/env bash

case $1 in
  "examples" )
    enabled_examples=(
      "bf"
      "collatz"
      "fizzbuzz"
      "generic"
      "overloads"
      "slice"
      "struct"
    )

    for i in ${enabled_examples[@]}; do
      echo "Running example ${i}.ym"
      build/yumec example/${i}.ym
      mv output.ll result-${i}.ll
      ./yume.out
    done;;

  "coverage" )
    llvm-profdata-13 merge -sparse default.profraw -o default.profdata
    llvm-cov-13 show ./yume_test -instr-profile default.profdata -Xdemangler c++filt -Xdemangler -i -format html -output-dir cov -ignore-filename-regex '/(test|usr)/.*'
esac
