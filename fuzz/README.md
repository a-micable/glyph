# Fuzzing

This directory contains libFuzzer entry points and seed corpora for Glyph Atlas.

## `unpack_fuzzer`

`unpack_fuzzer` consumes a `GFZA` command archive. Each record contains an operation code and a payload. Glyph payloads are written to a temporary file and passed through file-backed APIs such as load, validation, render, unpack, atlas export, and manifest generation.

## `kerning_fuzzer`

`kerning_fuzzer` consumes a two-revision archive. The first four bytes store the first revision size. The remaining bytes contain the first and second `.glyph` revisions. The harness passes both revisions to `glyph_reload_sequence_resolve`.

## `scene_fuzzer`

`scene_fuzzer` consumes a text scene document. Documents contain viewport dimensions, nested scene nodes, and an operation log. Supported operations include scene snapshots, serialization, node movement, node deletion, handler registration, handler dispatch, handler deregistration, and schema promotion.

## Corpus

Per-target seed files live under `fuzz/corpus/<target>/`. The ClusterFuzzLite build script packages each target corpus into `$OUT/<target>_seed_corpus.zip`.
