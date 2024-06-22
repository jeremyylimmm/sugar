#include <stddef.h>
#include <stdlib.h>

#include "internal.h"

Arena init_arena(size_t size, void* memory) {
    return (Arena) {
        .base = (size_t)memory,
        .size = size
    };
}

void* arena_push(Arena* arena, size_t amount) {
    if (!amount) {
        return 0;
    }

    size_t result = arena->base + arena->allocated;
    result = (result + 7) & ~7;
    assert("arena out of memory" && (arena->size-result) >= amount);
    arena->allocated = result - arena->base + amount;
    return (void*)result;
}

void* arena_zero(Arena* arena, size_t amount) {
    void* data = arena_push(arena, amount);
    memset(data, 0, amount);
    return data;
}

Bitset* make_bitset(Arena* arena, size_t bit_count) {
    size_t word_count = (bit_count + 31) / 32;
    size_t structure_size = offsetof(Bitset, data) + word_count * sizeof(uint32_t);

    Bitset* set = arena_zero(arena, structure_size);
    set->word_count = word_count;
    set->bit_count = bit_count;

    return set;
}

void bitset_set(Bitset* set, size_t index){
    assert(index < set->bit_count);
    set->data[index / 32] |= 1 << (index % 32);
}

void bitset_unset(Bitset* set, size_t index) {
    assert(index < set->bit_count);
    set->data[index / 32] &= ~(1 << (index % 32));
}

bool bitset_get(Bitset* set, size_t index) {
    assert(index < set->bit_count);
    return (set->data[index / 32] >> (index % 32)) & 1;
}

void bitset_clear(Bitset* set) {
    memset(set->data, 0, set->word_count * sizeof(uint32_t));
}

void init_scratch_library(ScratchLibrary* library, size_t arena_size) {
    for (int i = 0; i < LENGTH(library->arenas); ++i) {
        library->arenas[i] = init_arena(arena_size, malloc(arena_size));
    }
}

Scratch scratch_get(ScratchLibrary* library, int conflict_count, Arena** conflicts) {
    for (int i = 0; i < LENGTH(library->arenas); ++i) {
        Arena* arena = &library->arenas[i];

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

    assert("no available scratch arenas" && false);
    return (Scratch){0};
}

void scratch_release(Scratch* scratch) {
    scratch->arena->allocated = scratch->allocated;
    memset(scratch, 0, sizeof(*scratch));
}

#define FNV_OFFSET_BASIS 0xcbf29ce484222325
#define FNV_PRIME 0x100000001b3

uint64_t fnv1a_hash(void* data, size_t length) {
    uint64_t hash = FNV_OFFSET_BASIS;

    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = ((uint8_t*)data)[i];
        hash = (hash & ~((uint64_t)0xff)) | ((uint8_t)hash ^ byte);
        hash *= FNV_PRIME;
    }

    return hash;
}