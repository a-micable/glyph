# Glyph Atlas

Glyph Atlas is a small C command-line tool for building, unpacking, and previewing bitmap font atlases stored in the `.glyph` format.

The input font directory is a set of grayscale PGM images named by glyph ID, such as `65.pgm` for `A`. Packing combines those images into one bitmap atlas and writes enough metadata to reconstruct each individual glyph later.

## Format Overview

A `.glyph` file is a little-endian binary file with these stages:

- Header: magic bytes, version, flags, glyph count, and kerning pair count.
- Glyph table: glyph ID, bounding box, bitmap data offset, and advance width for each glyph.
- Kerning table: left and right glyph indices plus a signed kerning offset.
- Atlas metadata: atlas dimensions, row allocation records, and per-glyph placement coordinates.
- Optional hinting block: per-glyph bytecode payloads when the hints flag is set.
- Atlas pixels: one 8-bit grayscale packed bitmap atlas.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The CLI binary is written to `build/glyph`.

## Usage

Pack a directory of PGM glyph images:

```sh
glyph pack <font_dir> out.glyph
```

Unpack a `.glyph` atlas back into individual PGM glyph files:

```sh
glyph unpack out.glyph <output_dir>
```

Render sample text to a grayscale PGM preview:

```sh
glyph render out.glyph "sample text" preview.pgm
```

Inspect or validate an existing file:

```sh
glyph info out.glyph
glyph validate out.glyph
```

Export the packed atlas image or a text manifest:

```sh
glyph atlas out.glyph atlas.pgm
glyph manifest out.glyph manifest.txt
```

Create a sample PGM font directory for experiments:

```sh
glyph sample sample-font 32 96
glyph pack sample-font sample.glyph
```

## Input Glyphs

Input glyphs must be PGM files in either `P5` binary or `P2` ASCII format. The filename stem is parsed as an unsigned integer glyph ID, so `48.pgm` maps to glyph ID `48`.

An optional `kerning.txt` file may be placed next to the glyph images. Each line is `left_id right_id offset`, for example `65 86 -1`.

## Known Limitations

The renderer is intentionally simple: it supports single-byte glyph IDs, grayscale output, horizontal layout, and basic positive kerning. It does not shape complex scripts, load TrueType or OpenType files, or emit color image formats.
