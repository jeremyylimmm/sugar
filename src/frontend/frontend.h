#pragma once

#include "internal.h"

enum {
    TOKEN_EOF,
    TOKEN_INT_LITERAL = 256,
    TOKEN_IDENTIFIER,
};

typedef struct {
    int kind;
    int length;
    char* start;
    int line;
} Token;

// Define op code enums

#define X(name, ...) HIR_OP_##name,

typedef enum {
    HIR_OP_ILLEGAL,

    #include "frontend_ops.inc"

    NUM_HIR_OPS
} HIR_OpCode;

#undef X

// Table to lookup op code ids/names

#define X(name, id, ...) id,

static char* hir_op_id[NUM_HIR_OPS] = {
    "<error op>",
    #include "frontend_ops.inc"
};

#undef X

// HIR structures

typedef struct HIR_Node HIR_Node;
typedef struct HIR_Block HIR_Block;

struct HIR_Node {
    HIR_Block* block;
    HIR_Node* prev;
    HIR_Node* next;

    HIR_OpCode op;

    int in_count;
    HIR_Node** ins;

    void* data;
    int tid;
};

struct HIR_Block {
    HIR_Block* next;
    HIR_Node* start;
    HIR_Node* end;
    int tid;
};

typedef struct {
    HIR_Block* control_flow_head;
} HIR_Proc;

// Functions

HIR_Proc* parse(Arena* arena, char* source_path, char* source);
void hir_print(HIR_Proc* proc);
void hir_append(HIR_Block* block, HIR_Node* node);