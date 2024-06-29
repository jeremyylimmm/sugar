#include <stdio.h>
#include <stdlib.h>

#include "frontend/frontend.h"

static Arena scratch_arenas[2];

Scratch get_global_scratch(int conflict_count, Arena** conflicts) {
    for (int i = 0; i < LENGTH(scratch_arenas); ++i) {
        Arena* arena = &scratch_arenas[i];

        bool does_conflict = false;

        for (int j = 0; j < conflict_count; ++j) {
            if (arena == conflicts[j]) {
                does_conflict = true;
                break;
            }
        }

        if (!does_conflict) {
            return (Scratch) {
                .arena = arena,
                .allocated = arena->allocated
            };
        }
    }

    assert(false);
    return (Scratch){0};
}

int main() {
    size_t arena_size = 5 * 1024  * 1024;
    Arena arena = init_arena(arena_size, malloc(arena_size));

    for (int i = 0; i < LENGTH(scratch_arenas); ++i) {
        scratch_arenas[i] = init_arena(arena_size, malloc(arena_size));
    }

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

    HIR_Proc* hir_proc = parse(&arena, source_path, source);
    if (!hir_proc) {
        return 1;
    }

    hir_print(hir_proc);

    SB_Context* sbc = sb_init();
    SB_Proc* lir_proc = hir_lower(sbc, hir_proc);

    sb_visualize(sbc, lir_proc);
    sb_opt(sbc, lir_proc);
    sb_visualize(sbc, lir_proc);

    sb_generate_x64(sbc, lir_proc);

    return 0;
}