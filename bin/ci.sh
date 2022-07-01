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
    done
esac
