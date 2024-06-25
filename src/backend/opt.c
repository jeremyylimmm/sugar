#include <stdlib.h>
#include <stdio.h>

#include "sb.h"
#include "sb_internal.h"

typedef enum {
    EMPTY,
    OCCUPIED,
    REMOVED
} HashSlotState;

typedef struct {
    int count;
    int capacity;

    int* keys;
    int* values;
    HashSlotState* states;
} IndexTable;

typedef struct {
    int count;
    int capacity;
    SB_Node** data;

    IndexTable index_table;
} WorkList;


static bool index_table_set_static(int capacity, int* keys, int* values, HashSlotState* states, int id, int index) {
    int i = fnv1a_hash(&id, sizeof(id)) % capacity;

    for (int j = 0; j < capacity; ++j)
    {
        switch (states[i]) {
            case EMPTY:
            case REMOVED:
                keys[i] = id;
                values[i] = index;
                states[i] = OCCUPIED;
                return true;
            case OCCUPIED:
                if (keys[i] == id) {
                    values[i] = index;
                    return false;
                }
                break;
        }

        i = (i + 1) % capacity;
    }

    assert("hash table insertion failure" && false);
    return false;
}

static void index_table_make_room(IndexTable* table) {
    if (!table->capacity || load_factor(table->count, table->capacity) > 0.5f)
    {
        int new_capacity = table->capacity ? table->capacity * 2 : 8;

        int* new_keys = calloc(new_capacity, sizeof(int));
        int* new_values = calloc(new_capacity, sizeof(int));
        HashSlotState* new_states = calloc(new_capacity, sizeof(HashSlotState));

        for (int i = 0; i < table->capacity; ++i) {
            if (table->states[i] == OCCUPIED)
            {
                int id = table->keys[i];
                int index = table->values[i];

                index_table_set_static(new_capacity, new_keys, new_values, new_states, id, index);
            }
        }

        free(table->keys);
        free(table->values);
        free(table->states);

        table->capacity = new_capacity;
        table->keys = new_keys;
        table->values = new_values;
        table->states = new_states;
    }
}

static void index_table_set(IndexTable* table, int id, int index)
{
    index_table_make_room(table);

    if (index_table_set_static(table->capacity, table->keys, table->values, table->states, id, index)) {
        table->count++;
    }
}

static int _index_table_hash_find(IndexTable* table, int id) {
    if (!table->capacity) {
        return -1;
    }

    int i = fnv1a_hash(&id, sizeof(id)) % table->capacity;

    for (int j = 0; j < table->capacity; ++j) {
        switch (table->states[i]) {
            case EMPTY:
                return -1;
            case OCCUPIED:
                if (table->keys[i] == id) {
                    return i;
                }
                break;
        }

        i = (i + 1) % table->capacity;
    }

    return -1;
}

static int index_table_remove(IndexTable* table, int id) {
    int i = _index_table_hash_find(table, id);

    if (i == -1) {
        return -1;
    }

    int return_value = table->values[i];
    table->states[i] = REMOVED;
    table->count--;

    return return_value;
}

static int index_table_get(IndexTable* table, int id) {
    int i = _index_table_hash_find(table, id);

    if (i == -1) {
        return -1;
    }

    return table->values[i];
}

static void index_table_free(IndexTable* table) {
    free(table->keys);
    free(table->values);
    free(table->states);
    memset(table, 0, sizeof(*table));
}

static void work_list_add(WorkList* work_list, SB_Node* node) {
    if (index_table_get(&work_list->index_table, node->id) != -1) {
        return;
    }

    if (work_list->count == work_list->capacity)
    {
        work_list->capacity = work_list->capacity ? work_list->capacity * 2 : 8;
        work_list->data = realloc(work_list->data, work_list->capacity * sizeof(SB_Node*));
    }

    int index = work_list->count++;
    work_list->data[index] = node;

    index_table_set(&work_list->index_table, node->id, index);
}

static void work_list_remove(WorkList* work_list, SB_Node* node) {
    int index = index_table_get(&work_list->index_table, node->id);

    if (index != -1) {
        SB_Node* last = work_list->data[index] = work_list->data[--work_list->count];
        index_table_set(&work_list->index_table, last->id, index);
        index_table_remove(&work_list->index_table, node->id);
    }
}

static bool work_list_empty(WorkList* work_list) {
    return work_list->count == 0;
}

static SB_Node* work_list_pop(WorkList* work_list) {
    assert(work_list->count);
    SB_Node* result = work_list->data[--work_list->count];
    index_table_remove(&work_list->index_table, result->id);
    return result;
}

static void work_list_free(WorkList* work_list) {
    free(work_list->data);
    index_table_free(&work_list->index_table);
    memset(work_list, 0, sizeof(*work_list));
}

static bool work_list_has(WorkList* work_list, SB_Node* node) {
    return index_table_get(&work_list->index_table, node->id) != -1;
}

static void _work_list_init(WorkList* work_list, SB_Node* node) {
    if (work_list_has(work_list, node)) {
        return;
    }

    work_list_add(work_list, node);

    for (int i = 0; i < node->in_count; ++i) {
        if (node->_ins[i]) {
            _work_list_init(work_list, node->_ins[i]);
        }
    }
}

static void work_list_init(WorkList* work_list, SB_Proc* proc) {
    _work_list_init(work_list, proc->end);
}

typedef SB_Node* (*IdealizeFunction)(WorkList*, SB_Context*, SB_Node*);

static SB_Node* _idealize_phi(WorkList* work_list, SB_Context* context, SB_Node* node) {
    (void)context;

    SB_Node* same = 0;

    for (int i = 1; i < node->in_count; ++i) {
        SB_Node* input = node->_ins[i];

        if (!input || input == node) {
            continue;
        }

        if (!same) {
            same = input;
        }

        if (same != input) {
            return node;
        }
    }

    work_list_add(work_list, node->_ins[0]); // Region can be eliminated

    return same;
}

static SB_Node* _idealize_region(WorkList* work_list, SB_Context* context, SB_Node* node) {
    (void)context;
    (void)work_list;

    for (SB_User* user = node->users; user; user = user->next) {
        if (user->node->op == SB_OP_PHI && user->index == 0) { // Can't eliminate region if phis depend on it
            return node;
        }
    }

    SB_Node* same = 0;

    for (int i = 0; i < node->in_count; ++i) {
        SB_Node* input = node->_ins[i];

        if (!input) {
            continue;
        }

        if (!same) {
            same = input;
        }

        if (same != input) {
            return node;
        }
    }

    return same;
}

static IdealizeFunction idealize_table[NUM_SB_OPS] = {
    [SB_OP_PHI] = _idealize_phi,
    [SB_OP_REGION] = _idealize_region,
};

static void queue_users(WorkList* work_list, SB_Node* node) {
    for (SB_User* user = node->users; user; user = user->next) {
        work_list_add(work_list, user->node);
    }
}

static void delete_node(SB_Node* node) {
    assert("cannot delete node, has users" && !node->users);

    for (int i = 0; i < node->in_count; ++i) {
        SB_Node* input = node->_ins[i];

        if (!input) {
            continue;
        }

        for (SB_User** user = &input->users; *user;) {
            if ((*user)->node == node && (*user)->index == i) {
                *user = (*user)->next;
            }
            else {
                user = &(*user)->next;
            }
        }
    }

    for (int i = 0; i < node->in_count; ++i) {
        SB_Node* input = node->_ins[i];

        if (!input) {
            continue;
        }

        if (!input->users) {
            delete_node(input);
        }
    }
}

static void replace_node(SB_Node* target, SB_Node* source) {
    while (target->users) {
        SB_User* user = target->users;
        target->users = user->next;

        user->node->_ins[user->index] = source;
        user->next = source->users;
        source->users = user;
    }

    delete_node(target);
}

void sb_opt(SB_Context* context, SB_Proc* proc) {
    (void)context;

    WorkList work_list = {0};
    work_list_init(&work_list, proc);

    while(!work_list_empty(&work_list)) {
        SB_Node* node = work_list_pop(&work_list);

        if (idealize_table[node->op]) {
            SB_Node* ideal = idealize_table[node->op](&work_list, context, node);

            if (ideal != node) {
                queue_users(&work_list, node);
                replace_node(node, ideal);
            }
        }
    }

    work_list_free(&work_list);
}