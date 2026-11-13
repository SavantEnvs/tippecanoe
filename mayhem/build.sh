#!/usr/bin/env bash
#
# mayhem/build.sh — builds, inside the commit image (as `mayhem`, in /mayhem):
#   1. a sanitized copy of tippecanoe (ASan+UBSan, DWARF 3, SanCov) in mayhem/build/san, from which
#      the libFuzzer harnesses, their -standalone reproducers and the sanitized CLI tools used by the
#      file-input Mayhemfiles are linked;
#   2. the project's own `make all` with its NORMAL flags, in tree, which mayhem/test.sh runs.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
BUILD="$SRC/mayhem/build"
SAN="$BUILD/san"

# 1) sanitized project, built out of tree (the upstream Makefile writes objects next to the sources)
rm -rf "$BUILD"
mkdir -p "$SAN"
tar --anchored --exclude=./mayhem --exclude=./tests --exclude=./.git \
    --exclude='*.o' --exclude='*.d' \
    --exclude=./tippecanoe --exclude='./tippecanoe-*' --exclude=./tile-join --exclude=./unit \
    -cf - . | tar -C "$SAN" -xf -

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$BUILD/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$BUILD/standalone_main.o"

FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -include $SRC/mayhem/protozero_assert_off.h"
(
  cd "$SAN"
  CFLAGS="$FUZZ_CFLAGS" CXXFLAGS="$FUZZ_CFLAGS" LDFLAGS="$BUILD/lsan_off.o" \
    make -j"$MAYHEM_JOBS" RELEASE_FLAGS=-O1 \
      tippecanoe tippecanoe-decode tippecanoe-overzoom tile-join
)

# sanitized CLI tools, driven through @@ by the file-input Mayhemfiles
for tool in tippecanoe tippecanoe-decode tippecanoe-overzoom tile-join; do
  cp "$SAN/$tool" "$SRC/fuzz-$tool"
done

# 2) harnesses: libFuzzer binary + standalone (run-once) reproducer each
LIBOBJS=(text.o sort.o mvt.o projection.o clip.o attribute.o jsonpull/jsonpull.o evaluator.o
         read_json.o csv.o clipper2/src/clipper.engine.o)
for h in fuzz_jsonpull fuzz_mvt_decode fuzz_csv; do
  objs=()
  for o in "${LIBOBJS[@]}"; do objs+=("$SAN/$o"); done
  $CXX -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE -include "$SRC/mayhem/protozero_assert_off.h" \
      -I"$SAN" -I"$SAN/clipper2/include" "$SRC/mayhem/$h.cpp" "${objs[@]}" "$BUILD/lsan_off.o" \
      -lm -lz -lsqlite3 -lpthread -o "$SRC/$h"
  $CXX -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS -include "$SRC/mayhem/protozero_assert_off.h" \
      -I"$SAN" -I"$SAN/clipper2/include" "$SRC/mayhem/$h.cpp" "$BUILD/standalone_main.o" \
      "${objs[@]}" "$BUILD/lsan_off.o" -lm -lz -lsqlite3 -lpthread -o "$SRC/$h-standalone"
done

# 3) the project's own binaries and unit tests, NORMAL flags, in tree, for mayhem/test.sh
CFLAGS="$COVERAGE_FLAGS" CXXFLAGS="$COVERAGE_FLAGS" LDFLAGS="$COVERAGE_FLAGS" \
  make -j"$MAYHEM_JOBS" all
