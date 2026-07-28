#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
    FILE *fp = stdin;
    uint8_t *data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    int c;

    if (argc > 1) {
        fp = fopen(argv[1], "rb");
        if (!fp) {
            return 1;
        }
    }

    while ((c = fgetc(fp)) != EOF) {
        if (size == capacity) {
            size_t next_capacity = capacity ? capacity * 2 : 4096;
            uint8_t *next = (uint8_t *)realloc(data, next_capacity);
            if (!next) {
                free(data);
                if (fp != stdin) {
                    fclose(fp);
                }
                return 1;
            }
            data = next;
            capacity = next_capacity;
        }
        data[size++] = (uint8_t)c;
    }

    if (fp != stdin) {
        fclose(fp);
    }
    (void)LLVMFuzzerTestOneInput(data, size);
    free(data);
    return 0;
}
