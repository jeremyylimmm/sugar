#include <stdio.h>

#include "frontend.h"

typedef void(*PrintOverload)(HIR_Node*);

static void print_overload_integer_literal(HIR_Node* node) {
    printf("%d", *(int*)node->data);
}

static PrintOverload print_overloads[NUM_HIR_OPS] = {
    [HIR_OP_INTEGER_LITERAL] = print_overload_integer_literal
};

void hir_print(HIR_Proc* proc) {
    int block_counter = 0;
    int node_counter = 0;

    for (HIR_Block* block = proc->control_flow_head; block; block = block->next) {
        block->tid = block_counter++;

        for (HIR_Node* node = block->start; node; node = node->next) {
            node->tid = node_counter++;
        }
    }

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