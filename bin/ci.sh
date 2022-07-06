#!/usr/bin/env bash

declare BUILD_DIR
declare LLVM_PROFDATA
declare LLVM_COV

case $1 in
  "examples" )
    for i in example/*; do
      [[ -f "${i}" ]] || continue
      echo "Running example ${i}"
      "${BUILD_DIR}"/yumec "${i}" || exit 1
      mv output.ll result-"$(basename "${i}")".ll
      ./yume.out || exit 1
    done;;

  "test" )
    cd "${BUILD_DIR}" || exit
    ./yume_test -r junit::out=junit.xml -r console '~[!shouldfail]';;

  "coverage" )
    cd "${BUILD_DIR}" || exit
    ${LLVM_PROFDATA} merge -sparse default.profraw -o default.profdata
    ${LLVM_COV} show ./yume_test -instr-profile default.profdata -Xdemangler c++filt -Xdemangler -i -format html -output-dir cov -ignore-filename-regex '/(test|usr)/.*';;

  *)
    echo "Invalid usage"
esac
