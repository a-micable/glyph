# Glyph Atlas Architecture

Glyph Atlas is structured as a modular C library with a small CLI driver and a consistent data pipeline for bitmap font atlas creation.

## Core sub-systems

- `src/atlas.*` — file layout, header management, and atlas serialization.
- `src/bitmap.*` — image primitives, blitting, transformations, and raw PGM handling.
- `src/pack_plan.*` — atlas packing heuristics, row allocation, and placement strategy.
- `src/layout.*` — text layout, wrapping, alignment, and hit testing.
- `src/validate.*` — file integrity checks, rule-based diagnostics, and validation report generation.
- `src/metrics.*` — structured metrics collection for glyph coverage, atlas density, and kerning analytics.
- `src/logging.*` — structured runtime logging with text and JSON output modes.
- `src/config.*` — environment-aware runtime configuration and guardrails for CLI behavior.

## Design principles

- Strong separation between core library APIs and CLI tooling.
- Explicit ownership for heap memory and cleanup across file objects.
- Diagnostics designed for both machine-readable JSON and human-readable text.
- Build-time flags for sanitizers and static analysis.

## Release-ready features

- `--version` exposes semantic build information.
- CMake install targets are provided for reusable library deployment.
- Continuous integration builds with sanitizers and cross-compiler validation.
