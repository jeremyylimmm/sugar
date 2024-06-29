#include <stdio.h>

#include "sb_internal.h"

static GCM_Block* make_block(Arena* arena) {
    return arena_type(arena, GCM_Block);
}

static GCM_Block* build_control_flow_graph(Arena* arena, Bitset* visited, SB_Node* node, GCM_Block** head, GCM_Block* current, GCM_Block** assignment) {
    if (bitset_get(visited, node->id)) {
        return assignment[node->id];
    }

    bitset_set(visited, node->id);

    bool new_block = false;

    if (node->flags & SB_NODE_FLAG_STARTS_BLOCK) {
        current = make_block(arena);
        new_block = true;
    }

    assignment[node->id] = current;

    for (SB_User* user = node->users; user; user = user->next) {
        if (!(user->node->flags & SB_NODE_FLAG_PRODUCES_CONTROL)) {
            continue;
        }

        GCM_Block* result = build_control_flow_graph(arena, visited, user->node, head, current, assignment);

        if (result != current) {
            current->successors[current->successor_count++] = result;
            result->predecessor_count++;
        }
    }

    if (new_block) {
        current->next = *head;
        *head = current;
    }

    return current;
}

static int assign_tids(GCM_Block* control_flow_head) {
    int count = 0;
    for (GCM_Block* block = control_flow_head; block; block = block->next) {
        block->tid = count++;
    }
    return count;
}

static GCM_Block* intersect(GCM_Block* a, GCM_Block* b) {
    GCM_Block* finger1 = a;
    GCM_Block* finger2 = b;

    while (finger1 != finger2) {
        while (finger1->tid > finger2->tid) {
            finger1 = finger1->immediate_dominator;
        }

        while (finger2->tid > finger1->tid) {
            finger2 = finger2->immediate_dominator;
        }
    }

    return finger1;
}

static void build_dominator_tree(GCM_Block* control_flow_head) {
    control_flow_head->immediate_dominator = control_flow_head;

    while (true) {
        bool changed = false;

        for (GCM_Block* block = control_flow_head->next; block; block = block->next) {
            int first_predecessor = -1;

            for (int i = 0; i < block->predecessor_count; ++i) {
                if (block->predecessors[i]->immediate_dominator) {
                    first_predecessor = i;
                    break;
                }
            }

            assert(first_predecessor != -1);
            GCM_Block* new_idom = block->predecessors[first_predecessor];

            for (int i = 0; i < block->predecessor_count; ++i) {
                if (i == first_predecessor || !block->predecessors[i]->immediate_dominator) {
                    continue;
                }

                GCM_Block* p = block->predecessors[i];
                new_idom = intersect(p, new_idom);
            }

            if (new_idom != block->immediate_dominator) {
                block->immediate_dominator = new_idom;
                changed = true;
            }
        }

        if (!changed) {
            break;
        }
    }

    control_flow_head->immediate_dominator = 0;
}

static void get_predecessors(Arena* arena, GCM_Block* control_flow_head) {
    for (GCM_Block* block = control_flow_head; block; block = block->next) {
        block->predecessors = arena_array(arena, GCM_Block*, block->predecessor_count);
        block->predecessor_count = 0;
    }

    for (GCM_Block* block = control_flow_head; block; block = block->next) {
        for (int i = 0; i < block->successor_count; ++i) {
            GCM_Block* successor = block->successors[i];
            successor->predecessors[successor->predecessor_count++] = block;
        }
    }
}

GCM_Block* global_code_motion(Arena* arena, SB_Context* context, SB_Proc* proc) {
    Scratch scratch = scratch_get(&context->scratch_library, 1, &arena);

    GCM_Block* control_flow_head = 0;
    Bitset* visited = make_bitset(scratch.arena, context->next_id);
    GCM_Block** assignment = arena_array(scratch.arena, GCM_Block*, context->next_id);

    build_control_flow_graph(arena, visited, proc->start, &control_flow_head, 0, assignment);
    assign_tids(control_flow_head);

    get_predecessors(arena, control_flow_head);
    build_dominator_tree(control_flow_head);

    scratch_release(&scratch);

    gcm_print(control_flow_head);

    return control_flow_head;
}

void gcm_print(GCM_Block* control_flow_head) {
    assign_tids(control_flow_head);

    for (GCM_Block* block = control_flow_head; block; block = block->next) {
        printf("bb_%d:\n", block->tid);

        if (block->immediate_dominator) {
            printf("  idom: bb_%d\n", block->immediate_dominator->tid);
        }

        if (block->successor_count == 1) {
            printf("  jmp bb_%d\n", block->successors[0]->tid);
        }
    }
}