#include "internal.h"

Arena arena_init(size_t size, void* memory) {
    return (Arena) {
        .base = (size_t)memory,
        .size = size
    };
}

void* arena_push(Arena* arena, size_t amount) {
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