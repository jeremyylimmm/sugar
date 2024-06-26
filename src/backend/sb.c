#include <stdlib.h>
#include <stdio.h>

#include "sb_internal.h"
#include "sb.h"

#define ARENA_SIZE (5 * 1024 * 1024)

SB_Context* sb_init() {
    Arena arena = init_arena(ARENA_SIZE, malloc(ARENA_SIZE));

    SB_Context* context = arena_type(&arena, SB_Context);
    context->arena = arena;
    init_scratch_library(&context->scratch_library, ARENA_SIZE);

    return context;
}

static void mark_useful(Bitset* useful, SB_Node* node) {
    if (bitset_get(useful, node->id)) {
        return;
    }

    bitset_set(useful, node->id);

    for (int i = 0; i < node->in_count; ++i) {
        if (node->_ins[i]) {
            mark_useful(useful, node->_ins[i]);
        }
    }
}

static void trim(Bitset* trimmed, Bitset* useful, SB_Node* node) {
    if (bitset_get(trimmed, node->id)) {
        return;
    }

    bitset_set(trimmed, node->id);

    for (SB_User** user = &node->users; *user;)
    {
        if (bitset_get(useful, (*user)->node->id)) {
            user = &(*user)->next;
        }
        else {
            *user = (*user)->next;
        }
    }

    for (int i = 0; i < node->in_count; ++i) {
        if (node->_ins[i]) {
            trim(trimmed, useful, node->_ins[i]);
        }
    }
}

SB_Proc* sb_make_proc(SB_Context* context, SB_Node* start, SB_Node* end) {
    Scratch scratch = scratch_get(&context->scratch_library, 0, 0);

    Bitset* useful  = make_bitset(scratch.arena, context->next_id);
    Bitset* trimmed = make_bitset(scratch.arena, context->next_id);

    mark_useful(useful, end);

    if (!bitset_get(useful, start->id)) {
        assert("start not reachable from end" && false);
        return 0;
    }

    trim(trimmed, useful, end);

    SB_Proc* proc = arena_type(&context->arena, SB_Proc);

    proc->start = start;
    proc->end = end;

    scratch_release(&scratch);
    return proc;
}

static void allocate_ins(SB_Context* context, SB_Node* node, int in_count) {
    assert(!node->in_count);
    node->in_count = in_count;
    node->_ins = arena_array(&context->arena, SB_Node*, in_count);
}

static SB_Node* make_node(SB_Context* context, SB_OpCode op, int in_count) {
    SB_Node* node = arena_type(&context->arena, SB_Node);

    node->id = context->next_id++;  
    node->op = op;

    allocate_ins(context, node, in_count);

    return node;
}

static void assign_input(SB_Context* context, SB_Node* node, SB_Node* input, int index) {
    assert(index < node->in_count);
    assert(!node->_ins[index]);

    node->_ins[index] = input;

    SB_User* user = arena_type(&context->arena, SB_User);
    user->node = node;
    user->index = index;

    user->next = input->users;
    input->users = user;
}

#define SET_INPUT(node, index, value) assign_input(context, node, value, index)

SB_Node* sb_node_start(SB_Context* context) {
    return make_node(context, SB_OP_START, 0);
}

enum {
    END_CONTROL,
    END_STORE,
    END_RETURN_VALUE,
    NUM_END_INS
};

SB_Node* sb_node_end(SB_Context* context, SB_Node* control, SB_Node* store, SB_Node* return_value) {
    SB_Node* node = make_node(context, SB_OP_END, NUM_END_INS);
    SET_INPUT(node, END_CONTROL, control);
    SET_INPUT(node, END_STORE, store);
    SET_INPUT(node, END_RETURN_VALUE, return_value);
    return node;
}

static void init_data_field(SB_Context* context, SB_Node* node, int size) {
    node->data_size = size;
    node->data = arena_push(&context->arena, size);
}

SB_Node* sb_node_null(SB_Context* context) {
    return make_node(context, SB_OP_NULL, 0);
}

SB_Node* sb_node_integer_constant(SB_Context* context, uint64_t value) {
    SB_Node* node = make_node(context, SB_OP_INTEGER_CONSTANT, 0);
    init_data_field(context, node, sizeof(value));
    memcpy(node->data, &value, sizeof(value));
    return node;
}

SB_Node* sb_node_alloca(SB_Context* context) {
    return make_node(context, SB_OP_ALLOCA, 0);
}

enum {
    BINARY_LEFT,
    BINARY_RIGHT,
    NUM_BINARY_INS
};

static SB_Node* make_binary(SB_Context* context, SB_OpCode op, SB_Node* left, SB_Node* right) {
    SB_Node* node = make_node(context, op, NUM_BINARY_INS);
    SET_INPUT(node, BINARY_LEFT, left);
    SET_INPUT(node, BINARY_RIGHT, right);
    return node;
}

SB_Node* sb_node_add(SB_Context* context, SB_Node* left, SB_Node* right) {
    return make_binary(context, SB_OP_ADD, left, right);
}

SB_Node* sb_node_sub(SB_Context* context, SB_Node* left, SB_Node* right) {
    return make_binary(context, SB_OP_SUB, left, right);
}

SB_Node* sb_node_mul(SB_Context* context, SB_Node* left, SB_Node* right) {
    return make_binary(context, SB_OP_MUL, left, right);
}

SB_Node* sb_node_sdiv(SB_Context* context, SB_Node* left, SB_Node* right) {
    return make_binary(context, SB_OP_SDIV, left, right);
}

enum {
    LOAD_CONTROL,
    LOAD_STORE,
    LOAD_ADDRESS,
    NUM_LOAD_INS
};

SB_Node* sb_node_load(SB_Context* context, SB_Node* control, SB_Node* store, SB_Node* address) {
    SB_Node* node = make_node(context, SB_OP_LOAD, NUM_LOAD_INS);
    SET_INPUT(node, LOAD_CONTROL, control);
    SET_INPUT(node, LOAD_STORE, store);
    SET_INPUT(node, LOAD_ADDRESS, address);
    return node;
}

enum {
    STORE_CONTROL,
    STORE_STORE,
    STORE_ADDRESS,
    STORE_VALUE,
    NUM_STORE_INS
};

SB_Node* sb_node_store(SB_Context* context, SB_Node* control, SB_Node* store, SB_Node* address, SB_Node* value) {
    SB_Node* node = make_node(context, SB_OP_STORE, NUM_STORE_INS);
    SET_INPUT(node, STORE_CONTROL, control);
    SET_INPUT(node, STORE_STORE, store);
    SET_INPUT(node, STORE_ADDRESS, address);
    SET_INPUT(node, STORE_VALUE, value);
    return node;
}

enum {
    PROJECTION_INPUT,
    NUM_PROJECTION_INS
};

SB_Node* sb_node_start_control(SB_Context* context, SB_Node* start) {
    assert(start->op == SB_OP_START);
    SB_Node* node = make_node(context, SB_OP_START_CONTROL, NUM_PROJECTION_INS);
    SET_INPUT(node, PROJECTION_INPUT, start);
    return node;
}

SB_Node* sb_node_start_store(SB_Context* context, SB_Node* start) {
    assert(start->op == SB_OP_START);
    SB_Node* node = make_node(context, SB_OP_START_STORE, NUM_PROJECTION_INS);
    SET_INPUT(node, PROJECTION_INPUT, start);
    return node;
}

enum {
    BRANCH_CONTROL,
    BRANCH_PREDICATE,
    NUM_BRANCH_INS
};

SB_Node* sb_node_branch(SB_Context* context, SB_Node* control, SB_Node* predicate) {
    SB_Node* node = make_node(context, SB_OP_BRANCH, NUM_BRANCH_INS);
    SET_INPUT(node, BRANCH_CONTROL, control);
    SET_INPUT(node, BRANCH_PREDICATE, predicate);
    return node;
}

SB_Node* sb_node_region(SB_Context* context) {
    return make_node(context, SB_OP_REGION, 0);
}

SB_Node* sb_node_phi(SB_Context* context) {
    return make_node(context, SB_OP_PHI, 0);
}

void sb_set_region_inputs(SB_Context* context, SB_Node* region, int input_count, SB_Node** inputs) {
    assert(region->op == SB_OP_REGION);

    allocate_ins(context, region, input_count);
    
    for (int i = 0; i < input_count; ++i) {
        SET_INPUT(region, i, inputs[i]);
    }
}

void sb_set_phi_inputs(SB_Context* context, SB_Node* phi, SB_Node* region, int input_count, SB_Node** inputs) {
    assert(phi->op == SB_OP_PHI);
    assert(region->op == SB_OP_REGION);

    allocate_ins(context, phi, input_count + 1);

    SET_INPUT(phi, 0, region);

    for (int i = 0; i < input_count; ++i) {
        SET_INPUT(phi, i + 1, inputs[i]);
    }
}

SB_Node* sb_node_branch_true(SB_Context* context, SB_Node* branch) {
    assert(branch->op == SB_OP_BRANCH);
    SB_Node* node = make_node(context, SB_OP_BRANCH_TRUE, NUM_PROJECTION_INS);
    SET_INPUT(node, PROJECTION_INPUT, branch);
    return node;
}

SB_Node* sb_node_branch_false(SB_Context* context, SB_Node* branch) {
    assert(branch->op == SB_OP_BRANCH);
    SB_Node* node = make_node(context, SB_OP_BRANCH_FALSE, NUM_PROJECTION_INS);
    SET_INPUT(node, PROJECTION_INPUT, branch);
    return node;
}

static void graphviz(Bitset* visited, SB_Node* node) {
    if (bitset_get(visited, node->id)) {
        return;
    }

    bitset_set(visited, node->id);

    printf("  n%d [shape=\"record\",label=\"", node->id);

    if (node->in_count == 0) {
        printf("%s", sb_op_name[node->op]);
    }
    else {
        printf("{{");

        for (int i = 0; i < node->in_count; ++i) {
            if (i > 0) {
                printf("|");
            }

            printf("<i%d>%d", i, i);
        }

        printf("}|%s}", sb_op_name[node->op]);
    }

    printf("\"];\n");

    for (int i = 0; i < node->in_count; ++i) {
        if (node->_ins[i]) {
            graphviz(visited, node->_ins[i]);
            printf("  n%d -> n%d:i%d\n", node->_ins[i]->id, node->id, i);
        }
    }
}

void sb_visualize(SB_Context* context, SB_Proc* proc) {
    Scratch scratch = scratch_get(&context->scratch_library, 0, 0);

    printf("digraph G {\n");

    Bitset* visited = make_bitset(scratch.arena, context->next_id);
    graphviz(visited, proc->end);

    printf("}\n\n");

    scratch_release(&scratch);
}