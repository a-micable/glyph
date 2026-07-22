#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

#define MAX_GLYPHS 256
#define MAX_KERN_PAIRS 4096

typedef struct {
    uint32_t gid;
    int16_t bx;
    int16_t by;
    uint16_t gw;
    uint16_t gh;
    uint32_t offset;
    int16_t advance;
    uint16_t padding;
} glyph_t;

typedef struct {
    uint32_t left;
    uint32_t right;
    int16_t offset;
    uint16_t padding;
} kern_pair_t;

typedef struct {
    uint32_t y;
    uint32_t height;
    uint32_t used;
} row_t;

typedef struct {
    uint32_t x;
    uint32_t y;
} placement_t;

static uint8_t* buffer = NULL;
static size_t buffer_size = 0;
static size_t buffer_pos = 0;

void buffer_init(size_t size) {
    buffer_size = size;
    buffer_pos = 0;
    buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        exit(1);
    }
}

void buffer_free() {
    free(buffer);
    buffer = NULL;
    buffer_size = 0;
    buffer_pos = 0;
}

void buffer_write_u16(uint16_t value) {
    if (buffer_pos + 2 > buffer_size) {
        fprintf(stderr, "Buffer overflow\n");
        exit(1);
    }
    buffer[buffer_pos++] = value & 0xFF;
    buffer[buffer_pos++] = (value >> 8) & 0xFF;
}

void buffer_write_i16(int16_t value) {
    buffer_write_u16((uint16_t)value);
}

void buffer_write_u32(uint32_t value) {
    if (buffer_pos + 4 > buffer_size) {
        fprintf(stderr, "Buffer overflow\n");
        exit(1);
    }
    buffer[buffer_pos++] = value & 0xFF;
    buffer[buffer_pos++] = (value >> 8) & 0xFF;
    buffer[buffer_pos++] = (value >> 16) & 0xFF;
    buffer[buffer_pos++] = (value >> 24) & 0xFF;
}

void buffer_write_bytes(const uint8_t* data, size_t len) {
    if (buffer_pos + len > buffer_size) {
        fprintf(stderr, "Buffer overflow\n");
        exit(1);
    }
    memcpy(buffer + buffer_pos, data, len);
    buffer_pos += len;
}

void buffer_write_u8(uint8_t value) {
    if (buffer_pos + 1 > buffer_size) {
        fprintf(stderr, "Buffer overflow\n");
        exit(1);
    }
    buffer[buffer_pos++] = value;
}

void make_glyph(int count, kern_pair_t* kern_pairs, int kern_count, 
                int width, int hints, int reorder, int tall_every,
                uint8_t** out_data, size_t* out_size) {
    glyph_t glyphs[MAX_GLYPHS];
    placement_t placements[MAX_GLYPHS];
    row_t rows[MAX_GLYPHS];
    int glyph_count = 0;
    int row_count = 0;
    
    int x = 0;
    int y = 0;
    int row_h = 0;
    int row_used = 0;
    
    for (int i = 0; i < count; i++) {
        int gw = 3 + ((i * 5 + reorder) % 9);
        int gh = 4 + ((i * 7 + reorder) % 11);
        if (tall_every && i % tall_every == 0) {
            gh += 5;
        }
        if (x && x + gw > width) {
            rows[row_count].y = y;
            rows[row_count].height = row_h;
            rows[row_count].used = row_used;
            row_count++;
            y += row_h;
            x = 0;
            row_h = 0;
            row_used = 0;
        }
        
        uint32_t gid = 32 + ((i + reorder) % (count > 0 ? count : 1));
        placements[glyph_count].x = x;
        placements[glyph_count].y = y;
        
        glyphs[glyph_count].gid = gid;
        glyphs[glyph_count].bx = 0;
        glyphs[glyph_count].by = 0;
        glyphs[glyph_count].gw = gw;
        glyphs[glyph_count].gh = gh;
        glyphs[glyph_count].offset = y * width + x;
        glyphs[glyph_count].advance = gw + 1;
        glyphs[glyph_count].padding = 0;
        
        x += gw;
        row_used = x;
        row_h = (row_h > gh) ? row_h : gh;
        glyph_count++;
    }
    
    if (count > 0) {
        rows[row_count].y = y;
        rows[row_count].height = row_h;
        rows[row_count].used = row_used;
        row_count++;
        y += row_h;
    } else {
        y = 1;
    }
    
    int atlas_h = y;
    
    size_t pixel_count = width * atlas_h;
    uint8_t* pixels = (uint8_t*)calloc(pixel_count, sizeof(uint8_t));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate pixels\n");
        exit(1);
    }
    
    for (int idx = 0; idx < glyph_count; idx++) {
        int gw = glyphs[idx].gw;
        int gh = glyphs[idx].gh;
        int px = placements[idx].x;
        int py = placements[idx].y;
        
        for (int yy = 0; yy < gh; yy++) {
            for (int xx = 0; xx < gw; xx++) {
                pixels[(py + yy) * width + px + xx] = 
                    (idx * 29 + xx * 13 + yy * 7 + 31) & 255;
            }
        }
    }
    
    size_t estimated_size = 4 + 2 + 2 + 4 + 4 + 
                           glyph_count * 20 + 
                           kern_count * 12 + 
                           12 + row_count * 12 + 
                           glyph_count * 8 + 
                           (hints ? glyph_count * 3 : 0) + 
                           pixel_count;
    
    buffer_init(estimated_size);
    
    buffer_write_bytes((const uint8_t*)"GLYP", 4);
    buffer_write_u16(1);
    buffer_write_u16(hints ? 1 : 0);
    buffer_write_u32(glyph_count);
    buffer_write_u32(kern_count);
    
    for (int i = 0; i < glyph_count; i++) {
        buffer_write_u32(glyphs[i].gid);
        buffer_write_i16(glyphs[i].bx);
        buffer_write_i16(glyphs[i].by);
        buffer_write_u16(glyphs[i].gw);
        buffer_write_u16(glyphs[i].gh);
        buffer_write_u32(glyphs[i].offset);
        buffer_write_i16(glyphs[i].advance);
        buffer_write_u16(glyphs[i].padding);
    }
    
    for (int i = 0; i < kern_count; i++) {
        buffer_write_u32(kern_pairs[i].left % (count > 0 ? count : 1));
        buffer_write_u32(kern_pairs[i].right % (count > 0 ? count : 1));
        buffer_write_i16(kern_pairs[i].offset);
        buffer_write_u16(kern_pairs[i].padding);
    }
    
    buffer_write_u32(width);
    buffer_write_u32(atlas_h);
    buffer_write_u32(row_count);
    
    for (int i = 0; i < row_count; i++) {
        buffer_write_u32(rows[i].y);
        buffer_write_u32(rows[i].height);
        buffer_write_u32(rows[i].used);
    }
    
    for (int i = 0; i < glyph_count; i++) {
        buffer_write_u32(placements[i].x);
        buffer_write_u32(placements[i].y);
    }
    
    if (hints) {
        for (int i = 0; i < glyph_count; i++) {
            if (i % 3 == 0) {
                uint8_t program[3];
                program[0] = (0x40 + i) & 255;
                program[1] = glyphs[i].gw & 255;
                program[2] = glyphs[i].gh & 255;
                buffer_write_u16(3);
                buffer_write_bytes(program, 3);
            } else {
                buffer_write_u16(0);
            }
        }
    }
    
    buffer_write_bytes(pixels, pixel_count);
    
    free(pixels);
    
    *out_data = buffer;
    *out_size = buffer_pos;
    buffer = NULL;
    buffer_size = 0;
    buffer_pos = 0;
}

int sparse_kern(int count, kern_pair_t* pairs) {
    int step = (count / 8) > 0 ? count / 8 : 1;
    int pair_count = 0;
    
    for (int i = 0; i < count; i += step) {
        pairs[pair_count].left = i;
        pairs[pair_count].right = (i + 1) % count;
        pairs[pair_count].offset = (i % 2) ? -1 : 1;
        pairs[pair_count].padding = 0;
        pair_count++;
    }
    
    return pair_count;
}

int dense_kern(int count, kern_pair_t* pairs) {
    int pair_count = 0;
    int limit = count < 16 ? count : 16;
    
    for (int left = 0; left < limit; left++) {
        for (int right = 0; right < limit; right++) {
            if (left != right) {
                pairs[pair_count].left = left;
                pairs[pair_count].right = right;
                pairs[pair_count].offset = (left - right) % 5 - 2;
                pairs[pair_count].padding = 0;
                pair_count++;
            }
        }
    }
    
    return pair_count;
}

void write_file(const char* path, uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", path);
        exit(1);
    }
    fwrite(data, 1, size, f);
    fclose(f);
}

void mkdir_p(const char* path) {
    char tmp[512];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int main() {
    const char* fixtures_dir = "tests/fixtures";
    const char* unpack_corpus = "fuzz/corpus/unpack_fuzzer";
    const char* kerning_corpus = "fuzz/corpus/kerning_fuzzer";
    
    mkdir_p(fixtures_dir);
    mkdir_p(unpack_corpus);
    mkdir_p(kerning_corpus);
    
    kern_pair_t kern_pairs[MAX_KERN_PAIRS];
    uint8_t* data;
    size_t size;
    int kern_count;
    
    // valid_4_no_kern.glyph
    kern_count = 0;
    make_glyph(4, kern_pairs, kern_count, 16, 0, 0, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/valid_4_no_kern.glyph", fixtures_dir);
        write_file(path, data, size);
        snprintf(path, sizeof(path), "%s/valid_4_no_kern.glyph", unpack_corpus);
        write_file(path, data, size);
    }
    free(data);
    
    // valid_16_sparse_hints.glyph
    kern_count = sparse_kern(16, kern_pairs);
    make_glyph(16, kern_pairs, kern_count, 24, 1, 0, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/valid_16_sparse_hints.glyph", fixtures_dir);
        write_file(path, data, size);
        snprintf(path, sizeof(path), "%s/valid_16_sparse_hints.glyph", unpack_corpus);
        write_file(path, data, size);
    }
    free(data);
    
    // valid_64_dense.glyph
    kern_count = dense_kern(64, kern_pairs);
    make_glyph(64, kern_pairs, kern_count, 48, 0, 0, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/valid_64_dense.glyph", fixtures_dir);
        write_file(path, data, size);
        snprintf(path, sizeof(path), "%s/valid_64_dense.glyph", unpack_corpus);
        write_file(path, data, size);
    }
    free(data);
    
    // valid_128_sparse_hints.glyph
    kern_count = sparse_kern(128, kern_pairs);
    make_glyph(128, kern_pairs, kern_count, 64, 1, 0, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/valid_128_sparse_hints.glyph", fixtures_dir);
        write_file(path, data, size);
        snprintf(path, sizeof(path), "%s/valid_128_sparse_hints.glyph", unpack_corpus);
        write_file(path, data, size);
    }
    free(data);
    
    // reload_a_pre.glyph
    kern_count = dense_kern(16, kern_pairs);
    make_glyph(16, kern_pairs, kern_count, 24, 0, 0, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_a_pre.glyph", fixtures_dir);
        write_file(path, data, size);
    }
    uint8_t* reload_a_pre = data;
    size_t reload_a_pre_size = size;
    
    // reload_a_post.glyph
    kern_count = dense_kern(9, kern_pairs);
    make_glyph(9, kern_pairs, kern_count, 24, 0, 5, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_a_post.glyph", fixtures_dir);
        write_file(path, data, size);
    }
    uint8_t* reload_a_post = data;
    size_t reload_a_post_size = size;
    
    // reload_a.bin
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_a.bin", kerning_corpus);
        buffer_init(4 + reload_a_pre_size + reload_a_post_size);
        buffer_write_u32(reload_a_pre_size);
        buffer_write_bytes(reload_a_pre, reload_a_pre_size);
        buffer_write_bytes(reload_a_post, reload_a_post_size);
        write_file(path, buffer, buffer_pos);
        buffer_free();
    }
    free(reload_a_pre);
    free(reload_a_post);
    
    // reload_b_pre.glyph
    kern_count = sparse_kern(64, kern_pairs);
    make_glyph(64, kern_pairs, kern_count, 32, 1, 1, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_b_pre.glyph", fixtures_dir);
        write_file(path, data, size);
    }
    uint8_t* reload_b_pre = data;
    size_t reload_b_pre_size = size;
    
    // reload_b_post.glyph
    kern_count = dense_kern(16, kern_pairs);
    make_glyph(16, kern_pairs, kern_count, 32, 1, 11, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_b_post.glyph", fixtures_dir);
        write_file(path, data, size);
    }
    uint8_t* reload_b_post = data;
    size_t reload_b_post_size = size;
    
    // reload_b.bin
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_b.bin", kerning_corpus);
        buffer_init(4 + reload_b_pre_size + reload_b_post_size);
        buffer_write_u32(reload_b_pre_size);
        buffer_write_bytes(reload_b_pre, reload_b_pre_size);
        buffer_write_bytes(reload_b_post, reload_b_post_size);
        write_file(path, buffer, buffer_pos);
        buffer_free();
    }
    free(reload_b_pre);
    free(reload_b_post);
    
    // reload_c_pre.glyph
    kern_count = sparse_kern(128, kern_pairs);
    make_glyph(128, kern_pairs, kern_count, 48, 0, 2, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_c_pre.glyph", fixtures_dir);
        write_file(path, data, size);
    }
    uint8_t* reload_c_pre = data;
    size_t reload_c_pre_size = size;
    
    // reload_c_post.glyph
    kern_pairs[0].left = 0;
    kern_pairs[0].right = 3;
    kern_pairs[0].offset = -2;
    kern_pairs[0].padding = 0;
    kern_pairs[1].left = 3;
    kern_pairs[1].right = 0;
    kern_pairs[1].offset = 2;
    kern_pairs[1].padding = 0;
    make_glyph(4, kern_pairs, 2, 16, 0, 20, 0, &data, &size);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_c_post.glyph", fixtures_dir);
        write_file(path, data, size);
    }
    uint8_t* reload_c_post = data;
    size_t reload_c_post_size = size;
    
    // reload_c.bin
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/reload_c.bin", kerning_corpus);
        buffer_init(4 + reload_c_pre_size + reload_c_post_size);
        buffer_write_u32(reload_c_pre_size);
        buffer_write_bytes(reload_c_pre, reload_c_pre_size);
        buffer_write_bytes(reload_c_post, reload_c_post_size);
        write_file(path, buffer, buffer_pos);
        buffer_free();
    }
    free(reload_c_pre);
    free(reload_c_post);
    
    printf("Fixtures generated successfully\n");
    return 0;
}
