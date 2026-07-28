# Glyph Atlas Architecture

Glyph Atlas is a C library and CLI for packing grayscale PGM glyph images into `.glyph` atlas files, reading those files back, and rendering simple previews.

## Modules

- `src/atlas.*` handles `.glyph` file loading, writing, packing, unpacking, and preview rendering.
- `src/header.*`, `src/glyph_table.*`, `src/kerning.*`, and `src/hints.*` encode and decode file sections.
- `src/row_alloc.*` tracks atlas row placement and reload-time glyph references.
- `src/bitmap.*` implements grayscale bitmap allocation, PGM I/O, drawing, transforms, and comparisons.
- `src/layout.*` shapes single-byte text into positioned glyph runs.
- `src/render_backend.*` renders layouts into bitmap surfaces.
- `src/validate.*` builds diagnostics and summary reports for loaded glyph files.
- `src/manifest.*`, `src/metrics.*`, and `src/coverage.*` derive text reports and aggregate data from glyph files.
- `src/edit.*` and `src/script.*` apply table edits and scripted transformations.
- `src/cache.*` and `src/advanced_cache.*` provide cache containers used by tooling and tests.
- `src/scene.*` parses text scene documents with nested nodes and an operation log.
- `src/tooling.*` implements CLI helper commands for info, validation, atlas export, manifests, and sample font generation.

## Data Flow

Packing reads PGM files from a directory, places glyph bitmaps into atlas rows, writes metadata tables, and appends atlas pixels. Loading reads the same sections into a `GlyphFile`. Tooling commands then inspect, validate, render, export, or transform that loaded object.

Fuzzing builds separate harnesses for glyph archive commands, reload sequences, and scene documents.
