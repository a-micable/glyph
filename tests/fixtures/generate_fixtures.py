#!/usr/bin/env python3
from pathlib import Path
import shutil
import struct


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "fixtures"
UNPACK_CORPUS = ROOT / "fuzz" / "corpus" / "unpack_fuzzer"
KERNING_CORPUS = ROOT / "fuzz" / "corpus" / "kerning_fuzzer"


def u16(value):
    return struct.pack("<H", value & 0xFFFF)


def i16(value):
    return struct.pack("<h", value)


def u32(value):
    return struct.pack("<I", value & 0xFFFFFFFF)


def make_glyph(count, kern_pairs, width=32, hints=False, reorder=0, tall_every=5):
    glyphs = []
    placements = []
    rows = []
    x = 0
    y = 0
    row_h = 0
    row_used = 0

    for i in range(count):
        gw = 3 + ((i * 5 + reorder) % 9)
        gh = 4 + ((i * 7 + reorder) % 11)
        if tall_every and i % tall_every == 0:
            gh += 5
        if x and x + gw > width:
            rows.append((y, row_h, row_used))
            y += row_h
            x = 0
            row_h = 0
            row_used = 0

        gid = 32 + ((i + reorder) % max(count, 1))
        placements.append((x, y))
        glyphs.append((gid, 0, 0, gw, gh, y * width + x, gw + 1))
        x += gw
        row_used = x
        row_h = max(row_h, gh)

    if count:
        rows.append((y, row_h, row_used))
        atlas_h = y + row_h
    else:
        atlas_h = 1

    pixels = bytearray(width * atlas_h)
    for idx, glyph in enumerate(glyphs):
        _, _, _, gw, gh, _, _ = glyph
        px, py = placements[idx]
        for yy in range(gh):
            for xx in range(gw):
                pixels[(py + yy) * width + px + xx] = (
                    idx * 29 + xx * 13 + yy * 7 + 31
                ) & 255

    flags = 1 if hints else 0
    out = bytearray(b"GLYP")
    out += u16(1) + u16(flags) + u32(count) + u32(len(kern_pairs))
    for gid, bx, by, gw, gh, offset, advance in glyphs:
        out += (
            u32(gid)
            + i16(bx)
            + i16(by)
            + u16(gw)
            + u16(gh)
            + u32(offset)
            + i16(advance)
            + u16(0)
        )
    for left, right, offset in kern_pairs:
        out += u32(left % max(count, 1)) + u32(right % max(count, 1)) + i16(offset) + u16(0)
    out += u32(width) + u32(atlas_h) + u32(len(rows))
    for row in rows:
        out += u32(row[0]) + u32(row[1]) + u32(row[2])
    for px, py in placements:
        out += u32(px) + u32(py)
    if hints:
        for i, glyph in enumerate(glyphs):
            program = bytes([(0x40 + i) & 255, glyph[3] & 255, glyph[4] & 255]) if i % 3 == 0 else b""
            out += u16(len(program)) + program
    out += pixels
    return bytes(out)


def sparse(count):
    step = max(1, count // 8)
    return [(i, (i + 1) % count, -1 if i % 2 else 1) for i in range(0, count, step)]


def dense(count):
    pairs = []
    limit = min(count, 16)
    for left in range(limit):
        for right in range(limit):
            if left != right:
                pairs.append((left, right, (left - right) % 5 - 2))
    return pairs


def main():
    for directory in (FIXTURES, UNPACK_CORPUS, KERNING_CORPUS):
        directory.mkdir(parents=True, exist_ok=True)

    files = {
        "valid_4_no_kern.glyph": make_glyph(4, [], width=16, hints=False),
        "valid_16_sparse_hints.glyph": make_glyph(16, sparse(16), width=24, hints=True),
        "valid_64_dense.glyph": make_glyph(64, dense(64), width=48, hints=False),
        "valid_128_sparse_hints.glyph": make_glyph(128, sparse(128), width=64, hints=True),
        "reload_a_pre.glyph": make_glyph(16, dense(16), width=24, hints=False, reorder=0),
        "reload_a_post.glyph": make_glyph(9, dense(9), width=24, hints=False, reorder=5),
        "reload_b_pre.glyph": make_glyph(64, sparse(64), width=32, hints=True, reorder=1),
        "reload_b_post.glyph": make_glyph(16, dense(16), width=32, hints=True, reorder=11),
        "reload_c_pre.glyph": make_glyph(128, sparse(128), width=48, hints=False, reorder=2),
        "reload_c_post.glyph": make_glyph(4, [(0, 3, -2), (3, 0, 2)], width=16, hints=False, reorder=20),
    }

    for name, data in files.items():
        (FIXTURES / name).write_bytes(data)

    for name in (
        "valid_4_no_kern.glyph",
        "valid_16_sparse_hints.glyph",
        "valid_64_dense.glyph",
        "valid_128_sparse_hints.glyph",
    ):
        shutil.copyfile(FIXTURES / name, UNPACK_CORPUS / name)

    for stem in ("a", "b", "c"):
        first = files[f"reload_{stem}_pre.glyph"]
        second = files[f"reload_{stem}_post.glyph"]
        (KERNING_CORPUS / f"reload_{stem}.bin").write_bytes(u32(len(first)) + first + second)


if __name__ == "__main__":
    main()
