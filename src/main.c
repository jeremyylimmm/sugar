#include <stdio.h>
#include <stdlib.h>

#include "frontend/frontend.h"

int main() {
    char* source_path = "examples/test.sg";

    FILE* file;
    if (fopen_s(&file, source_path, "r")) {
        printf("Failed to load '%s'\n", source_path);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char* source = malloc(file_size + 1);
    size_t source_size = fread(source, 1, file_size, file);
    source[source_size] = '\0';

    size_t arena_size = 5 * 1024  * 1024;
    Arena arena = arena_init(arena_size, malloc(arena_size));

    HIR_Proc* proc = parse(&arena, source_path, source);
    if (!proc) {
        return 1;
    }

    hir_print(proc);

    return 0;
}