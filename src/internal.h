#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <memory.h>

#define LENGTH(array) (sizeof(array)/sizeof((array)[0]))

typedef struct {
    size_t base;
    size_t allocated;
    size_t size;
} Arena;

Arena arena_init(size_t size, void* memory);
void* arena_push(Arena* arena, size_t amount);
void* arena_zero(Arena* arena, size_t amount);
#define arena_type(arena, type) ((type*)arena_zero(arena, sizeof(type)))
#define arena_array(arena, type, count) ((type*)arena_zero(arena, (count) * sizeof(type)))