#!/bin/bash -eu

SRC_DIR="${SRC:-$(pwd)}"
OUT_DIR="${OUT:?OUT must be set}"
BUILD_DIR="${WORK:-$OUT_DIR}/glyph-atlas-build"

mkdir -p "$OUT_DIR" "$BUILD_DIR"
cd "$SRC_DIR"

CORE_SOURCES=(
  src/header.c
  src/binio.c
  src/glyph_table.c
  src/kerning.c
  src/atlas.c
  src/row_alloc.c
  src/hints.c
  src/reload.c
  src/bitmap.c
  src/cache.c
  src/compress.c
  src/coverage.c
  src/diagnostics.c
  src/filters.c
  src/fontdb.c
  src/layout.c
  src/manifest.c
  src/pack_plan.c
  src/edit.c
  src/metrics.c
  src/optimize.c
  src/render_backend.c
  src/script.c
  src/scene.c
  src/validate.c
  src/tooling.c
)

OBJECTS=()
for source in "${CORE_SOURCES[@]}"; do
  object="$BUILD_DIR/$(basename "$source" .c).o"
  $CC $CFLAGS -Isrc -c "$source" -o "$object"
  OBJECTS+=("$object")
done

$CC $CFLAGS $LIB_FUZZING_ENGINE fuzz/unpack_fuzzer.c "${OBJECTS[@]}" -lm -o "$OUT_DIR/unpack_fuzzer"
$CC $CFLAGS $LIB_FUZZING_ENGINE fuzz/kerning_fuzzer.c "${OBJECTS[@]}" -lm -o "$OUT_DIR/kerning_fuzzer"
$CC $CFLAGS $LIB_FUZZING_ENGINE fuzz/scene_fuzzer.c "${OBJECTS[@]}" -lm -o "$OUT_DIR/scene_fuzzer"

for target in unpack_fuzzer kerning_fuzzer scene_fuzzer; do
  corpus_dir="fuzz/corpus/$target"
  seed_zip="$OUT_DIR/${target}_seed_corpus.zip"
  if [ -d "$corpus_dir" ]; then
    if command -v zip >/dev/null 2>&1; then
      (cd "$corpus_dir" && zip -q -r "$OUT_DIR/${target}_seed_corpus.zip" .)
    elif command -v python3 >/dev/null 2>&1; then
      python3 - "$corpus_dir" "$seed_zip" <<'PY'
import os
import sys
import zipfile

corpus_dir, seed_zip = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(seed_zip, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(corpus_dir):
        for name in files:
            path = os.path.join(root, name)
            zf.write(path, os.path.relpath(path, corpus_dir))
PY
    fi
  fi
done
