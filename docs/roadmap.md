# Glyph Atlas Work Areas

This file lists project areas that are represented in the source tree.

## File Format

- `.glyph` headers, glyph tables, kerning tables, hint programs, row metadata, placements, and atlas pixels.
- PGM input and output for individual glyph images and rendered previews.

## CLI Commands

- `pack` builds a `.glyph` file from PGM glyph images.
- `unpack` writes packed glyphs back to PGM files.
- `render` writes a grayscale preview for input text.
- `info`, `validate`, `atlas`, and `manifest` inspect or export data from existing files.
- `sample` writes a sample PGM font directory.

## Library Areas

- Bitmap operations and image filters.
- Atlas packing and row allocation.
- Layout and rendering.
- Metrics, coverage, manifests, and diagnostics.
- Edit scripts and table transformations.
- Cache containers and cache reports.
- Scene document parsing and operation replay.

## Fuzz Targets

- `unpack_fuzzer` exercises file-backed glyph operations from a command archive.
- `kerning_fuzzer` exercises reload resolution across two glyph revisions.
- `scene_fuzzer` exercises scene parsing and operation dispatch.
