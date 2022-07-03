#!/usr/bin/env bash

declare BUILD_DIR

case $1 in
  "examples" )
    for i in example/*; do
      [[ -f "${i}" ]] || continue
      echo "Running example ${i}.ym"
      "${BUILD_DIR}"/yumec "${i}" || exit 1
      mv output.ll result-"$(basename "${i}")".ll
      rm output.ll
      ./yume.out
    done;;

  "test" )
    cd "${BUILD_DIR}" || exit
    ./yume_test -r junit::out=junit.xml -r console '~[!shouldfail]';;

  "coverage" )
    cd "${BUILD_DIR}" || exit
    llvm-profdata-13 merge -sparse default.profraw -o default.profdata
    llvm-cov-13 show ./yume_test -instr-profile default.profdata -Xdemangler c++filt -Xdemangler -i -format html -output-dir cov -ignore-filename-regex '/(test|usr)/.*';;

  *)
    echo "Invalid usage"
esac
