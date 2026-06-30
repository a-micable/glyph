#!/bin/bash -eu
$CC $CFLAGS -I./src -c src/*.c
$CC $CFLAGS $LIB_FUZZING_ENGINE fuzz/unpack_fuzzer.c *.o -o $OUT/unpack_fuzzer
$CC $CFLAGS $LIB_FUZZING_ENGINE fuzz/kerning_fuzzer.c *.o -o $OUT/kerning_fuzzer
