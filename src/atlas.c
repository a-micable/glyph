#include "atlas.h"

#include "header.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static void free_rows_copy(RowDescriptor *rows, uint32_t count) {
    if (!rows) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        free(rows[i].pixels);
    }
    free(rows);
}

void glyph_images_free(GlyphImageSet *set) {
    for (uint32_t i = 0; i < set->count; i++) {
        free(set->images[i].pixels);
    }
    free(set->images);
    set->images = NULL;
    set->count = 0;
}

void glyph_file_free(GlyphFile *file) {
    glyph_table_free(&file->glyphs);
    kerning_table_free(&file->kerning);
    hints_free(&file->hints);
    free_rows_copy(file->rows, file->row_count);
    free(file->placements);
    free(file->atlas_pixels);
    memset(file, 0, sizeof(*file));
}

static int next_token(FILE *fp, char *buf, size_t cap) {
    int c;
    do {
        c = fgetc(fp);
        if (c == '#') {
            while (c != '\n' && c != EOF) {
                c = fgetc(fp);
            }
        }
    } while (isspace(c));
    if (c == EOF) {
        return 0;
    }
    size_t n = 0;
    while (c != EOF && !isspace(c)) {
        if (n + 1 < cap) {
            buf[n++] = (char)c;
        }
        c = fgetc(fp);
    }
    buf[n] = 0;
    return n > 0;
}

static int read_pgm(const char *path, GlyphImage *img) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    char tok[64];
    if (!next_token(fp, tok, sizeof(tok))) {
        fclose(fp);
        return 0;
    }
    int binary = strcmp(tok, "P5") == 0;
    if (!binary && strcmp(tok, "P2") != 0) {
        fclose(fp);
        return 0;
    }
    if (!next_token(fp, tok, sizeof(tok))) {
        fclose(fp);
        return 0;
    }
    long w = strtol(tok, NULL, 10);
    if (!next_token(fp, tok, sizeof(tok))) {
        fclose(fp);
        return 0;
    }
    long h = strtol(tok, NULL, 10);
    if (!next_token(fp, tok, sizeof(tok))) {
        fclose(fp);
        return 0;
    }
    long maxv = strtol(tok, NULL, 10);
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || maxv <= 0 || maxv > 255) {
        fclose(fp);
        return 0;
    }
    img->width = (uint16_t)w;
    img->height = (uint16_t)h;
    img->advance = (int16_t)(w + 1);
    img->pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (!img->pixels) {
        fclose(fp);
        return 0;
    }
    if (binary) {
        if (fread(img->pixels, 1, (size_t)w * (size_t)h, fp) != (size_t)w * (size_t)h) {
            fclose(fp);
            return 0;
        }
    } else {
        for (long i = 0; i < w * h; i++) {
            if (!next_token(fp, tok, sizeof(tok))) {
                fclose(fp);
                return 0;
            }
            long v = strtol(tok, NULL, 10);
            img->pixels[i] = (uint8_t)((v * 255) / maxv);
        }
    }
    fclose(fp);
    return 1;
}

int glyph_write_pgm(const char *path, uint32_t width, uint32_t height, const uint8_t *pixels) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "P5\n%u %u\n255\n", width, height);
    int ok = fwrite(pixels, 1, (size_t)width * height, fp) == (size_t)width * height;
    fclose(fp);
    return ok;
}

static int has_pgm_suffix(const char *name) {
    size_t n = strlen(name);
    return n > 4 && strcmp(name + n - 4, ".pgm") == 0;
}

static int image_cmp(const void *a, const void *b) {
    const GlyphImage *ga = (const GlyphImage *)a;
    const GlyphImage *gb = (const GlyphImage *)b;
    return (ga->id > gb->id) - (ga->id < gb->id);
}

static int load_images_from_dir(const char *dir_path, GlyphImageSet *set) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }
    memset(set, 0, sizeof(*set));
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!has_pgm_suffix(ent->d_name)) {
            continue;
        }
        char *end = NULL;
        unsigned long id = strtoul(ent->d_name, &end, 10);
        if (end == ent->d_name || id > UINT32_MAX) {
            continue;
        }
        GlyphImage *next = (GlyphImage *)realloc(set->images, (set->count + 1) * sizeof(GlyphImage));
        if (!next) {
            closedir(dir);
            glyph_images_free(set);
            return 0;
        }
        set->images = next;
        memset(&set->images[set->count], 0, sizeof(GlyphImage));
        set->images[set->count].id = (uint32_t)id;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
        if (!read_pgm(path, &set->images[set->count])) {
            closedir(dir);
            glyph_images_free(set);
            return 0;
        }
        set->count++;
    }
    closedir(dir);
    qsort(set->images, set->count, sizeof(GlyphImage), image_cmp);
    return set->count > 0;
}

static uint32_t choose_atlas_width(const GlyphImageSet *set) {
    uint32_t total = 0;
    uint32_t maxw = 16;
    for (uint32_t i = 0; i < set->count; i++) {
        total += set->images[i].width;
        if (set->images[i].width > maxw) {
            maxw = set->images[i].width;
        }
    }
    uint32_t width = 32;
    while (width < maxw || (width < 1024 && width * width < total * 32)) {
        width *= 2;
    }
    return width;
}

static int compose_atlas(const GlyphImageSet *images, GlyphFile *file, RowAllocator *alloc) {
    memset(file, 0, sizeof(*file));
    file->flags = 0;
    if (!glyph_table_alloc(&file->glyphs, images->count)) {
        return 0;
    }
    file->placements = (GlyphPlacement *)calloc(images->count, sizeof(GlyphPlacement));
    if (!file->placements) {
        glyph_file_free(file);
        return 0;
    }
    row_alloc_init(alloc, choose_atlas_width(images));
    for (uint32_t i = 0; i < images->count; i++) {
        uint32_t x = 0;
        uint32_t y = 0;
        if (!row_alloc_place(alloc, images->images[i].width, images->images[i].height, &x, &y)) {
            glyph_file_free(file);
            return 0;
        }
        file->placements[i].x = x;
        file->placements[i].y = y;
        GlyphEntry *g = &file->glyphs.entries[i];
        g->id = images->images[i].id;
        g->width = images->images[i].width;
        g->height = images->images[i].height;
        g->advance = images->images[i].advance;
        g->bitmap_offset = y * alloc->width + x;
    }
    file->atlas_width = alloc->width;
    file->atlas_height = alloc->height;
    file->row_count = alloc->row_count;
    file->atlas_pixels = (uint8_t *)calloc((size_t)file->atlas_width * file->atlas_height, 1);
    file->rows = (RowDescriptor *)calloc(file->row_count, sizeof(RowDescriptor));
    if (!file->atlas_pixels || !file->rows) {
        glyph_file_free(file);
        return 0;
    }
    for (uint32_t r = 0; r < alloc->row_count; r++) {
        file->rows[r].y = alloc->rows[r].y;
        file->rows[r].height = alloc->rows[r].height;
        file->rows[r].used = alloc->rows[r].used;
    }
    for (uint32_t i = 0; i < images->count; i++) {
        const GlyphImage *img = &images->images[i];
        uint32_t px = file->placements[i].x;
        uint32_t py = file->placements[i].y;
        for (uint32_t yy = 0; yy < img->height; yy++) {
            memcpy(file->atlas_pixels + (size_t)(py + yy) * file->atlas_width + px,
                   img->pixels + (size_t)yy * img->width,
                   img->width);
        }
    }
    return row_alloc_cache_glyphs(alloc, &file->glyphs);
}

static int load_kerning_from_dir(const char *dir_path, GlyphFile *file) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/kerning.txt", dir_path);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return kerning_table_alloc(&file->kerning, 0);
    }

    KerningPair *pairs = NULL;
    uint32_t count = 0;
    uint32_t cap = 0;
    unsigned long left_id = 0;
    unsigned long right_id = 0;
    int offset = 0;
    while (fscanf(fp, "%lu %lu %d", &left_id, &right_id, &offset) == 3) {
        int left_index = glyph_table_find_id(&file->glyphs, (uint32_t)left_id);
        int right_index = glyph_table_find_id(&file->glyphs, (uint32_t)right_id);
        if (left_index < 0 || right_index < 0 || offset < INT16_MIN || offset > INT16_MAX) {
            fclose(fp);
            free(pairs);
            return 0;
        }
        if (count == cap) {
            uint32_t next_cap = cap ? cap * 2 : 8;
            KerningPair *next = (KerningPair *)realloc(pairs, next_cap * sizeof(KerningPair));
            if (!next) {
                fclose(fp);
                free(pairs);
                return 0;
            }
            pairs = next;
            cap = next_cap;
        }
        pairs[count].left_index = (uint32_t)left_index;
        pairs[count].right_index = (uint32_t)right_index;
        pairs[count].offset = (int16_t)offset;
        count++;
    }
    fclose(fp);
    file->kerning.pairs = pairs;
    file->kerning.count = count;
    return 1;
}

static int write_glyph_file(const char *path, const GlyphFile *file) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    GlyphHeader header = {GLYPH_VERSION, file->flags, file->glyphs.count, file->kerning.count};
    int ok = glyph_header_write(fp, &header) && glyph_table_write(fp, &file->glyphs) && kerning_table_write(fp, &file->kerning);
    glyph_write_u32(fp, file->atlas_width);
    glyph_write_u32(fp, file->atlas_height);
    glyph_write_u32(fp, file->row_count);
    for (uint32_t i = 0; ok && i < file->row_count; i++) {
        glyph_write_u32(fp, file->rows[i].y);
        glyph_write_u32(fp, file->rows[i].height);
        glyph_write_u32(fp, file->rows[i].used);
    }
    for (uint32_t i = 0; ok && i < file->glyphs.count; i++) {
        glyph_write_u32(fp, file->placements[i].x);
        glyph_write_u32(fp, file->placements[i].y);
    }
    if (ok && (file->flags & GLYPH_FLAG_HINTS)) {
        ok = hints_write(fp, &file->hints);
    }
    if (ok) {
        ok = fwrite(file->atlas_pixels, 1, (size_t)file->atlas_width * file->atlas_height, fp) == (size_t)file->atlas_width * file->atlas_height;
    }
    fclose(fp);
    return ok;
}

int glyph_pack_directory(const char *font_dir, const char *out_path) {
    GlyphImageSet images;
    if (!load_images_from_dir(font_dir, &images)) {
        return 0;
    }
    GlyphFile file;
    RowAllocator alloc;
    if (!compose_atlas(&images, &file, &alloc)) {
        glyph_images_free(&images);
        return 0;
    }
    int ok = load_kerning_from_dir(font_dir, &file) && write_glyph_file(out_path, &file);
    row_alloc_destroy(&alloc);
    glyph_file_free(&file);
    glyph_images_free(&images);
    return ok;
}

static int read_atlas_tail(FILE *fp, GlyphFile *file, uint16_t flags) {
    uint8_t buf[12];
    if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
        return 0;
    }
    size_t pos = 0;
    int ok = 1;
    file->atlas_width = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
    file->atlas_height = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
    file->row_count = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
    if (!ok || file->atlas_width == 0 || file->atlas_height == 0 || file->row_count > 4096) {
        return 0;
    }
    file->rows = (RowDescriptor *)calloc(file->row_count, sizeof(RowDescriptor));
    file->placements = (GlyphPlacement *)calloc(file->glyphs.count, sizeof(GlyphPlacement));
    if (!file->rows || !file->placements) {
        return 0;
    }
    for (uint32_t i = 0; i < file->row_count; i++) {
        if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
            return 0;
        }
        pos = 0;
        file->rows[i].y = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
        file->rows[i].height = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
        file->rows[i].used = glyph_read_u32(buf, sizeof(buf), &pos, &ok);
    }
    uint8_t pbuf[8];
    for (uint32_t i = 0; i < file->glyphs.count; i++) {
        if (fread(pbuf, 1, sizeof(pbuf), fp) != sizeof(pbuf)) {
            return 0;
        }
        pos = 0;
        file->placements[i].x = glyph_read_u32(pbuf, sizeof(pbuf), &pos, &ok);
        file->placements[i].y = glyph_read_u32(pbuf, sizeof(pbuf), &pos, &ok);
    }
    if (flags & GLYPH_FLAG_HINTS) {
        if (!hints_read(fp, &file->hints, file->glyphs.count)) {
            return 0;
        }
    }
    file->atlas_pixels = (uint8_t *)malloc((size_t)file->atlas_width * file->atlas_height);
    return file->atlas_pixels && fread(file->atlas_pixels, 1, (size_t)file->atlas_width * file->atlas_height, fp) == (size_t)file->atlas_width * file->atlas_height;
}

int glyph_load_file(const char *path, GlyphFile *file) {
    memset(file, 0, sizeof(*file));
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    GlyphHeader header;
    memset(&header, 0, sizeof(header));
    int ok = glyph_header_read(fp, &header);
    file->flags = header.flags;
    if (ok) {
        ok = glyph_table_read(fp, &file->glyphs, header.glyph_count);
    }
    if (ok) {
        ok = kerning_table_read(fp, &file->kerning, header.kerning_count);
    }
    if (ok) {
        ok = read_atlas_tail(fp, file, header.flags);
    }
    fclose(fp);
    if (!ok) {
        glyph_file_free(file);
    }
    return ok;
}

static int make_dir(const char *path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return 1;
    }
    return 0;
}

int glyph_unpack_file_to_dir(const char *glyph_path, const char *out_dir) {
    GlyphFile file;
    if (!glyph_load_file(glyph_path, &file) || !make_dir(out_dir)) {
        return 0;
    }
    int ok = 1;
    for (uint32_t i = 0; ok && i < file.glyphs.count; i++) {
        GlyphEntry *g = &file.glyphs.entries[i];
        uint8_t *pixels = (uint8_t *)calloc((size_t)g->width * g->height, 1);
        if (!pixels) {
            ok = 0;
            break;
        }
        uint32_t px = file.placements[i].x;
        uint32_t py = file.placements[i].y;
        for (uint32_t y = 0; y < g->height; y++) {
            if (px + g->width <= file.atlas_width && py + y < file.atlas_height) {
                memcpy(pixels + (size_t)y * g->width, file.atlas_pixels + (size_t)(py + y) * file.atlas_width + px, g->width);
            }
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%u.pgm", out_dir, g->id);
        ok = glyph_write_pgm(path, g->width, g->height, pixels);
        free(pixels);
    }
    glyph_file_free(&file);
    return ok;
}

int glyph_render_file(const char *glyph_path, const char *text, const char *out_pgm) {
    GlyphFile file;
    if (!glyph_load_file(glyph_path, &file)) {
        return 0;
    }
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t prev = UINT32_MAX;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int idx = glyph_table_find_id(&file.glyphs, *p);
        if (idx < 0) {
            width += 4;
            prev = UINT32_MAX;
            continue;
        }
        GlyphEntry *g = &file.glyphs.entries[idx];
        if (prev != UINT32_MAX) {
            width += (uint32_t)(kerning_lookup(&file.kerning, &file.glyphs, prev, *p) < 0 ? 0 : kerning_lookup(&file.kerning, &file.glyphs, prev, *p));
        }
        width += (uint32_t)(g->advance > 0 ? g->advance : g->width);
        if (g->height > height) {
            height = g->height;
        }
        prev = *p;
    }
    uint8_t *canvas = (uint8_t *)calloc((size_t)width * height, 1);
    if (!canvas) {
        glyph_file_free(&file);
        return 0;
    }
    uint32_t pen = 0;
    prev = UINT32_MAX;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int idx = glyph_table_find_id(&file.glyphs, *p);
        if (idx < 0) {
            pen += 4;
            prev = UINT32_MAX;
            continue;
        }
        GlyphEntry *g = &file.glyphs.entries[idx];
        int kern = prev == UINT32_MAX ? 0 : kerning_lookup(&file.kerning, &file.glyphs, prev, *p);
        if (kern > 0) {
            pen += (uint32_t)kern;
        }
        uint32_t sx = file.placements[idx].x;
        uint32_t sy = file.placements[idx].y;
        for (uint32_t y = 0; y < g->height; y++) {
            for (uint32_t x = 0; x < g->width; x++) {
                if (pen + x < width && sy + y < file.atlas_height && sx + x < file.atlas_width) {
                    uint8_t src = file.atlas_pixels[(size_t)(sy + y) * file.atlas_width + sx + x];
                    uint8_t *dst = &canvas[(size_t)y * width + pen + x];
                    if (src > *dst) {
                        *dst = src;
                    }
                }
            }
        }
        pen += (uint32_t)(g->advance > 0 ? g->advance : g->width);
        prev = *p;
    }
    int ok = glyph_write_pgm(out_pgm, width, height, canvas);
    free(canvas);
    glyph_file_free(&file);
    return ok;
}

int glyph_unpack(const uint8_t *data, size_t size) {
    GlyphFile file;
    memset(&file, 0, sizeof(file));
    GlyphHeader header;
    memset(&header, 0, sizeof(header));
    size_t pos = 0;
    int ok = glyph_header_parse_bytes(data, size, &pos, &header);
    file.flags = header.flags;
    if (ok) {
        ok = glyph_table_parse_bytes(data, size, &pos, &file.glyphs, header.glyph_count);
    }
    if (ok) {
        ok = kerning_table_parse_bytes(data, size, &pos, &file.kerning, header.kerning_count);
    }
    if (ok) {
        int rok = 1;
        file.atlas_width = glyph_read_u32(data, size, &pos, &rok);
        file.atlas_height = glyph_read_u32(data, size, &pos, &rok);
        file.row_count = glyph_read_u32(data, size, &pos, &rok);
        ok = rok && file.atlas_width > 0 && file.atlas_height > 0 && file.row_count <= 4096;
    }
    if (ok) {
        file.rows = (RowDescriptor *)calloc(file.row_count, sizeof(RowDescriptor));
        file.placements = (GlyphPlacement *)calloc(file.glyphs.count, sizeof(GlyphPlacement));
        ok = (file.row_count == 0 || file.rows) && (file.glyphs.count == 0 || file.placements);
    }
    for (uint32_t i = 0; ok && i < file.row_count; i++) {
        int rok = 1;
        file.rows[i].y = glyph_read_u32(data, size, &pos, &rok);
        file.rows[i].height = glyph_read_u32(data, size, &pos, &rok);
        file.rows[i].used = glyph_read_u32(data, size, &pos, &rok);
        ok = rok;
    }
    for (uint32_t i = 0; ok && i < file.glyphs.count; i++) {
        int pok = 1;
        file.placements[i].x = glyph_read_u32(data, size, &pos, &pok);
        file.placements[i].y = glyph_read_u32(data, size, &pos, &pok);
        ok = pok;
    }
    if (ok && (header.flags & GLYPH_FLAG_HINTS)) {
        ok = hints_parse_bytes(data, size, &pos, &file.hints, file.glyphs.count);
    }
    size_t atlas_size = (size_t)file.atlas_width * file.atlas_height;
    if (ok && atlas_size <= size && pos + atlas_size <= size) {
        file.atlas_pixels = (uint8_t *)malloc(atlas_size);
        ok = file.atlas_pixels != NULL;
        if (ok) {
            memcpy(file.atlas_pixels, data + pos, atlas_size);
        }
    } else {
        ok = 0;
    }
    glyph_file_free(&file);
    return ok;
}
// Final commit to reach 250
