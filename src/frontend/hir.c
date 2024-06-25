#include <stdio.h>

#include "frontend.h"

#define DATA(node, type) (*(type*)node->data)

typedef void(*PrintOverload)(HIR_Node*);

static void print_overload_integer_literal(HIR_Node* node) {
    printf("%d", DATA(node, int));
}

static void print_overload_jump(HIR_Node* node) {
    printf("jmp bb_%d", DATA(node, HIR_Block*)->tid);
}

static void print_overload_branch(HIR_Node* node) {
    HIR_Block** array = node->data;
    printf("br v%d, bb_%d, bb_%d", node->ins[0]->tid, array[0]->tid, array[1]->tid);
}

static PrintOverload print_overloads[NUM_HIR_OPS] = {
    [HIR_OP_INTEGER_LITERAL] = print_overload_integer_literal,
    [HIR_OP_JUMP] = print_overload_jump,
    [HIR_OP_BRANCH] = print_overload_branch,
};

typedef struct {
    int block_count;
    int node_count;
} TID_Counts;

static TID_Counts assign_tids(HIR_Proc* proc) {
    int block_counter = 0;
    int node_counter = 0;

    for (HIR_Block* block = proc->control_flow_head; block; block = block->next) {
        block->tid = block_counter++;

        for (HIR_Node* node = block->start; node; node = node->next) {
            node->tid = node_counter++;
        }
    }

    return (TID_Counts) {
        .block_count = block_counter,
        .node_count = node_counter
    };
}

void hir_print(HIR_Proc* proc) {
    assign_tids(proc);

    for (HIR_Block* block = proc->control_flow_head; block; block = block->next) {
        printf("bb_%d:\n", block->tid);

        for (HIR_Node* node = block->start; node; node = node->next) {
            printf("  v%d = ", node->tid);

            if (print_overloads[node->op]) {
                print_overloads[node->op](node);
            }
            else {
                printf("%s ", hir_op_id[node->op]);

                for (int i = 0; i < node->in_count; ++i) {
                    if (i > 0) {
                        printf(", ");
                    }

                    printf("v%d", node->ins[i]->tid);
                }
            }

            printf("\n");
        }
    }

    printf("\n");
}

static void fix_links(HIR_Node* node) {
    if (node->prev) {
        node->prev->next = node;
    }
    else {
        node->block->start = node;
    }

    if (node->next) {
        node->next->prev = node;
    }
    else {
        node->block->end = node;
    }
}

void hir_append(HIR_Block* block, HIR_Node* node) {
    assert(!node->block);
    node->block = block;
    node->prev = block->end;
    node->next = 0;
    fix_links(node);
}

void hir_remove(HIR_Node* node) {
    assert(node->block);

    if (node->prev) {
        node->prev->next = node->next;
    }
    else {
        node->block->start = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    }
    else {
        node->block->end = node->prev;
    }

    node->block = 0;
    node->prev = 0;
    node->next = 0;
}

typedef struct {
    int count;
    HIR_Block** data;
} BlockList;

typedef struct {
    int block_count;
    int node_count;
    BlockList* successors;
    BlockList* predecessors;
    Bitset* reachable;
} ProcInfo;

static void add_successor(Arena* arena, BlockList* succesors, int* predecessor_counts, HIR_Block* predecessor, HIR_Block* successor) {
    BlockList* list = &succesors[predecessor->tid];
    assert(list->count < 2);

    if (!list->data) {
        list->data = arena_array(arena, HIR_Block*, 2);
    }

    list->data[list->count++] = successor;

    predecessor_counts[successor->tid]++;
}

static void mark_reachable(Bitset* reachable, BlockList* successors, HIR_Block* block) {
    if (bitset_get(reachable, block->tid)) {
        return;
    }

    bitset_set(reachable, block->tid);

    for (int i = 0; i < successors[block->tid].count; ++i) {
        mark_reachable(reachable, successors, successors[block->tid].data[i]);
    }
}

static ProcInfo compute_proc_info(Arena* arena, HIR_Proc* hir_proc) {
    TID_Counts tids = assign_tids(hir_proc);

    BlockList* successors   = arena_array(arena, BlockList, tids.block_count);
    BlockList* predecessors = arena_array(arena, BlockList, tids.block_count);

    int* predecessor_counts = arena_array(arena, int, tids.block_count);

    for (HIR_Block* block = hir_proc->control_flow_head; block; block = block->next) {
        if (block->end) {
            HIR_Node* node = block->end;

            switch (node->op) {
                case HIR_OP_JUMP:
                    add_successor(arena, successors, predecessor_counts, block, *(HIR_Block**)node->data);
                    break;
                case HIR_OP_BRANCH: {
                    add_successor(arena, successors, predecessor_counts, block, ((HIR_Block**)node->data)[0]);
                    add_successor(arena, successors, predecessor_counts, block, ((HIR_Block**)node->data)[1]);
                } break;
            }
        }
    }

    for (int i = 0; i < tids.block_count; ++i) {
        predecessors[i].data = arena_array(arena, HIR_Block*, predecessor_counts[i]);
    }

    for (HIR_Block* block = hir_proc->control_flow_head; block; block = block->next) {
        BlockList* s = &successors[block->tid];
        for (int i = 0; i < s->count; ++i) {
            BlockList* p = &predecessors[s->data[i]->tid];
            p->data[p->count++] = block;
        }
    }

    Bitset* reachable = make_bitset(arena, tids.block_count);
    mark_reachable(reachable, successors, hir_proc->control_flow_head);

    return (ProcInfo) {
        .block_count = tids.block_count,
        .node_count = tids.node_count,
        .successors = successors,
        .predecessors = predecessors,
        .reachable = reachable
    };
}

typedef struct {
    int input_count;

    SB_Node** control_inputs;
    SB_Node** store_inputs;

    SB_Node* region;
    SB_Node* phi;
} BlockLowering;

typedef struct {
    SB_Node* control;
    SB_Node* store;
} Flow;

typedef struct {
    int count;
    SB_Node** control;
    SB_Node** store;
    SB_Node** return_value;
} ReturnState;

static void push_block_lowering_input(BlockLowering* block_lowerings, HIR_Block* block, SB_Node* control, SB_Node* store) {
    BlockLowering* bl = &block_lowerings[block->tid];
    int i = bl->input_count++;
    bl->control_inputs[i] = control;
    bl->store_inputs[i] = store;
}

static SB_Node* lower_node(SB_Context* context, SB_Node** mapping, SB_Node** return_value, Flow* flow, HIR_Node* node) {
    (void)flow;
    #define GET(node) mapping[node->tid]

    static_assert(NUM_HIR_OPS == 12, "not all hir ops handled");

    switch (node->op) {
        default:
            assert(false);
            return 0;

        case HIR_OP_INTEGER_LITERAL:
            return sb_node_integer_constant(context, (uint64_t)*(int*)node->data);

        case HIR_OP_ADD:
            return sb_node_add(context, GET(node->ins[0]), GET(node->ins[1]));
        case HIR_OP_SUB:
            return sb_node_sub(context, GET(node->ins[0]), GET(node->ins[1]));
        case HIR_OP_MUL:
            return sb_node_mul(context, GET(node->ins[0]), GET(node->ins[1]));
        case HIR_OP_DIV:
            return sb_node_sdiv(context, GET(node->ins[0]), GET(node->ins[1]));

        case HIR_OP_RETURN:
            *return_value = GET(node->ins[0]);
            return 0;

        case HIR_OP_JUMP:
        case HIR_OP_BRANCH:
            return 0;
    }
    
    #undef GET
}

static SB_Node* lower_block(SB_Context* context, SB_Node** mapping, Flow* flow, HIR_Block* block) {
    SB_Node* return_value = 0;

    for (HIR_Node* node = block->start; node; node = node->next) {
        mapping[node->tid] = lower_node(context, mapping, &return_value, flow, node);
    }
    
    return return_value;
}

SB_Proc* hir_lower(SB_Context* context, HIR_Proc* hir_proc) {
    (void)context;

    Scratch scratch = get_global_scratch(0, 0);
    ProcInfo proc_info = compute_proc_info(scratch.arena, hir_proc);

    BlockLowering* block_lowerings = arena_array(scratch.arena, BlockLowering, proc_info.block_count);
    SB_Node** mapping = arena_array(scratch.arena, SB_Node*, proc_info.node_count);

    ReturnState return_state = {
        .control      = arena_array(scratch.arena, SB_Node*, proc_info.block_count),
        .store        = arena_array(scratch.arena, SB_Node*, proc_info.block_count),
        .return_value = arena_array(scratch.arena, SB_Node*, proc_info.block_count),
    };

    for (int i = 0; i < proc_info.block_count; ++i) {
        BlockLowering* bl = &block_lowerings[i];

        int count = i == 0 ? 1 : proc_info.predecessors[i].count;

        bl->control_inputs = arena_array(scratch.arena, SB_Node*, count);
        bl->store_inputs   = arena_array(scratch.arena, SB_Node*, count);

        bl->phi    = sb_node_phi(context);
        bl->region = sb_node_region(context);
    }

    for (HIR_Block* block = hir_proc->control_flow_head; block; block = block->next) {
        if (!bitset_get(proc_info.reachable, block->tid)) {
            continue;
        }

        Flow flow = {
            .control = block_lowerings[block->tid].region,
            .store   = block_lowerings[block->tid].phi,
        };

        SB_Node* return_value = lower_block(context, mapping, &flow, block);

        SB_Node* control_outputs[2] = { flow.control, flow.control };

        if (block->end->op == HIR_OP_BRANCH) {
            SB_Node* branch = flow.control = sb_node_branch(context, flow.control, mapping[block->end->ins[0]->tid]);
            control_outputs[0] = sb_node_branch_true(context, branch);
            control_outputs[1] = sb_node_branch_false(context, branch);
        }

        for (int i = 0; i < proc_info.successors[block->tid].count; ++i)
        {
            HIR_Block* successor = proc_info.successors[block->tid].data[i];
            push_block_lowering_input(block_lowerings, successor, control_outputs[i], flow.store);
        }

        if (proc_info.successors[block->tid].count == 0) {
            if (!return_value) {
                return_value = sb_node_null(context);
            }

            int index = return_state.count++;
            return_state.control[index] = flow.control;
            return_state.store[index] = flow.store;
            return_state.return_value[index] = return_value;
        }
    }

    SB_Node* start = sb_node_start(context);

    SB_Node* start_control = sb_node_start_control(context, start);
    SB_Node* start_store = sb_node_start_store(context, start);
    push_block_lowering_input(block_lowerings, hir_proc->control_flow_head, start_control, start_store);

    for (int i = 0; i < proc_info.block_count; ++i) {
        BlockLowering* bl = &block_lowerings[i];
        sb_set_region_inputs(context, bl->region, bl->input_count, bl->control_inputs);
        sb_set_phi_inputs(context, bl->phi, bl->region, bl->input_count, bl->store_inputs);
    }
    
    SB_Node* end_region           = sb_node_region(context);
    SB_Node* end_phi_store        = sb_node_phi(context);
    SB_Node* end_phi_return_value = sb_node_phi(context);

    sb_set_region_inputs(context, end_region, return_state.count, return_state.control);
    sb_set_phi_inputs(context, end_phi_store, end_region, return_state.count, return_state.store);
    sb_set_phi_inputs(context, end_phi_return_value, end_region, return_state.count, return_state.return_value);

    SB_Node* end = sb_node_end(context, end_region, end_phi_store, end_phi_return_value);

    SB_Proc* proc = sb_make_proc(context, start, end);

    scratch_release(&scratch);

    return proc;
}