#pragma once

#include "internal.h"

struct SB_Context {
    Arena arena;
    int next_id;

    ScratchLibrary scratch_library;
};