#pragma once

#include "internal.h"
#include "sb.h"

#define X(name, mnemonic, ...) mnemonic,
static const char* sb_op_name[] = {
    "illegal",
    #include "ops.inc"
};
#undef X

struct SB_Context {
    Arena arena;
    int next_id;

    ScratchLibrary scratch_library;
};

typedef struct GCM_Node GCM_Node;
typedef struct GCM_Block GCM_Block;

struct GCM_Node {
    GCM_Block* block;
    GCM_Node* prev;
    GCM_Node* next;

    SB_Node* node;
};

struct GCM_Block {
    GCM_Block* next;
    
    int tid;

    int successor_count;
    int predecessor_count;

    GCM_Block* successors[2];
    GCM_Block** predecessors;

    GCM_Node* start;
    GCM_Node* end;

    GCM_Block* immediate_dominator;
};

GCM_Block* global_code_motion(Arena* arena, SB_Context* context, SB_Proc* proc);
void gcm_print(GCM_Block* control_flow_head);