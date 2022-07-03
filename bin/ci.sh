#!/usr/bin/env bash

case $1 in
  "examples" )
    for i in example/*; do
      [[ -f "${i}" ]] || continue
      echo "Running example ${i}.ym"
      build/yumec example/"${i}".ym
      mv output.ll result-"${i}".ll
      ./yume.out
    done;;

  "test" )
    cd ./build || exit
    ./yume_test -r junit::out=junit.xml -r console;;

  "coverage" )
    llvm-profdata-13 merge -sparse default.profraw -o default.profdata
    llvm-cov-13 show ./yume_test -instr-profile default.profdata -Xdemangler c++filt -Xdemangler -i -format html -output-dir cov -ignore-filename-regex '/(test|usr)/.*';;

  *)
    echo "Invalid usage"
esac
