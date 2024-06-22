#pragma once

#include <stdint.h>

#define X(name, ...) SB_OP_##name,
typedef enum {
    SB_OP_ILLEGAL,
    #include "ops.inc"
    NUM_SB_OPS
} SB_OpCode;
#undef X

#define X(name, mnemonic, ...) mnemonic,
static const char* sb_op_name[] = {
    "illegal",
    #include "ops.inc"
};
#undef X

typedef struct SB_User SB_User;
typedef struct SB_Node SB_Node;

struct SB_User {
    SB_User* next;
    SB_Node* node;
    int index;
};

struct SB_Node {
    int id;
    SB_OpCode op;

    int in_count;
    SB_Node** _ins;

    int data_size;
    void* data;

    SB_User* users;
};

typedef struct {
    SB_Node* start;
    SB_Node* end;
} SB_Proc;

typedef struct SB_Context SB_Context;

SB_Context* sb_init();

SB_Proc* sb_make_proc(SB_Context* context, SB_Node* start, SB_Node* end);

SB_Node* sb_node_start(SB_Context* context);
SB_Node* sb_node_end(SB_Context* context, SB_Node* control, SB_Node* store, SB_Node* return_value);

SB_Node* sb_node_null(SB_Context* context);
SB_Node* sb_node_integer_constant(SB_Context* context, uint64_t value);

SB_Node* sb_node_add(SB_Context* context, SB_Node* left, SB_Node* right);
SB_Node* sb_node_sub(SB_Context* context, SB_Node* left, SB_Node* right);
SB_Node* sb_node_mul(SB_Context* context, SB_Node* left, SB_Node* right);
SB_Node* sb_node_sdiv(SB_Context* context, SB_Node* left, SB_Node* right);

SB_Node* sb_node_start_control(SB_Context* context, SB_Node* start);
SB_Node* sb_node_start_store(SB_Context* context, SB_Node* start);

SB_Node* sb_node_branch(SB_Context* context, SB_Node* control, SB_Node* predicate);

SB_Node* sb_node_region(SB_Context* context);
SB_Node* sb_node_phi(SB_Context* context);

void sb_set_region_inputs(SB_Context* context, SB_Node* region, int input_count, SB_Node** inputs);
void sb_set_phi_inputs(SB_Context* context, SB_Node* phi, SB_Node* region, int input_count, SB_Node** inputs);

SB_Node* sb_node_branch_true(SB_Context* context, SB_Node* branch);
SB_Node* sb_node_branch_false(SB_Context* context, SB_Node* branch);

void sb_opt(SB_Context* context, SB_Proc* proc);

void sb_visualize(SB_Context* context, SB_Proc* proc);