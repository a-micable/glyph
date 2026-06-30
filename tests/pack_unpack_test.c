#include "atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int make_dir(const char *path) {
    return mkdir(path, 0777) == 0;
}

static int write_fixture_pgm(const char *path, unsigned w, unsigned h, unsigned seed) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "P5\n%u %u\n255\n", w, h);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            unsigned v = ((x + 1) * 31u + (y + 1) * 17u + seed) & 255u;
            fputc((int)v, fp);
        }
    }
    int ok = ferror(fp) == 0;
    fclose(fp);
    return ok;
}

static int write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    int ok = fwrite(text, 1, strlen(text), fp) == strlen(text);
    fclose(fp);
    return ok;
}

static unsigned char *read_all(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long n = ftell(fp);
    if (n < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)n);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size = (size_t)n;
    return buf;
}

static int same_file(const char *a, const char *b) {
    size_t as = 0;
    size_t bs = 0;
    unsigned char *ab = read_all(a, &as);
    unsigned char *bb = read_all(b, &bs);
    int ok = ab && bb && as == bs && memcmp(ab, bb, as) == 0;
    free(ab);
    free(bb);
    return ok;
}

int main(void) {
    const char *root = "pack_unpack_tmp";
    char in_dir[128];
    char out_dir[128];
    snprintf(in_dir, sizeof(in_dir), "%s/in", root);
    snprintf(out_dir, sizeof(out_dir), "%s/out", root);

    (void)mkdir(root, 0777);
    (void)mkdir(in_dir, 0777);
    (void)mkdir(out_dir, 0777);

    if (!write_fixture_pgm("pack_unpack_tmp/in/65.pgm", 5, 7, 1) ||
        !write_fixture_pgm("pack_unpack_tmp/in/66.pgm", 9, 4, 2) ||
        !write_fixture_pgm("pack_unpack_tmp/in/67.pgm", 3, 11, 3) ||
        !write_text_file("pack_unpack_tmp/in/kerning.txt", "65 66 1\n66 67 -1\n")) {
        fprintf(stderr, "failed to write pgm fixtures\n");
        return 1;
    }

    if (!glyph_pack_directory(in_dir, "pack_unpack_tmp/test.glyph")) {
        fprintf(stderr, "pack failed\n");
        return 1;
    }
    if (!glyph_unpack_file_to_dir("pack_unpack_tmp/test.glyph", out_dir)) {
        fprintf(stderr, "unpack failed\n");
        return 1;
    }
    if (!same_file("pack_unpack_tmp/in/65.pgm", "pack_unpack_tmp/out/65.pgm") ||
        !same_file("pack_unpack_tmp/in/66.pgm", "pack_unpack_tmp/out/66.pgm") ||
        !same_file("pack_unpack_tmp/in/67.pgm", "pack_unpack_tmp/out/67.pgm")) {
        fprintf(stderr, "round-trip pixel mismatch\n");
        return 1;
    }
    if (!glyph_render_file("pack_unpack_tmp/test.glyph", "ABC", "pack_unpack_tmp/preview.pgm")) {
        fprintf(stderr, "render failed\n");
        return 1;
    }

    size_t size = 0;
    unsigned char *data = read_all("pack_unpack_tmp/test.glyph", &size);
    int ok = data && glyph_unpack(data, size);
    free(data);
    if (!ok) {
        fprintf(stderr, "byte unpack failed\n");
        return 1;
    }
    return 0;
}
