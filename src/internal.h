#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <memory.h>
#include <string.h>

#define LENGTH(array) (sizeof(array)/sizeof((array)[0]))

#define BIT(x) (1 << (x))

typedef struct {
    size_t base;
    size_t allocated;
    size_t size;
} Arena;

Arena init_arena(size_t size, void* memory);
void* arena_push(Arena* arena, size_t amount);
void* arena_zero(Arena* arena, size_t amount);
#define arena_type(arena, type) ((type*)arena_zero(arena, sizeof(type)))
#define arena_array(arena, type, count) ((type*)arena_zero(arena, (count) * sizeof(type)))

typedef struct {
    Arena arenas[2];
} ScratchLibrary;

typedef struct {
    Arena* arena;
    size_t allocated;
} Scratch;

void init_scratch_library(ScratchLibrary* library, size_t arena_size);

Scratch scratch_get(ScratchLibrary* library, int conflict_count, Arena** conflicts);
void scratch_release(Scratch* scratch);

typedef struct {
    size_t bit_count;
    size_t word_count;
    uint32_t data[1];
} Bitset;

Bitset* make_bitset(Arena* arena, size_t bit_count);
void bitset_set(Bitset* set, size_t index);
void bitset_unset(Bitset* set, size_t index);
bool bitset_get(Bitset* set, size_t index);
void bitset_clear(Bitset* set);

uint64_t fnv1a_hash(void* data, size_t length);

inline float load_factor(int count, int capacity) {
    return (float)count/(float)capacity;
}

typedef struct {
    char* data;
    size_t length;
} String;

inline String make_string(Arena* arena, char* string) {
    size_t length = strlen(string);
    char* data = arena_push(arena, length + 1);

    memcpy(data, string, length);
    data[length] = 0;

    return (String) {
        .data = data,
        .length = length
    };
}

inline String string_view(char* string) {
    return (String) {
        .length = strlen(string),
        .data = string
    };
}

inline bool strings_identical(String a, String b) {
    return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}