#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "frontend.h"

typedef struct {
    Arena* arena;
    char* source_path;
    char* source;

    char* lexer_char;
    int lexer_line;
    Token lexer_cache;

    HIR_Block* control_flow_tail;

    Token last_rbrace;
} Parser;

static HIR_Block* make_block(Parser* p) {
    HIR_Block* block = arena_type(p->arena, HIR_Block);

    if (p->control_flow_tail) {
        p->control_flow_tail->next = block;
    }

    p->control_flow_tail = block;

    return block;
}

static int isident(char c) {
    return c == '_' || isalnum(c);
}

static int check_keyword(char* start, char* end, char* keyword, int kind) {
    size_t length = end - start;

    if (length == strlen(keyword) && memcmp(start, keyword, length) == 0) {
        return kind;
    }

    return TOKEN_IDENTIFIER;
}

static int identifier_kind(char* start, char* end) {
    switch (start[0]) {
        case 'r':
            return check_keyword(start, end, "return", TOKEN_KEYWORD_RETURN);
        case 'i':
            return check_keyword(start, end, "if", TOKEN_KEYWORD_IF);
        case 'e':
            return check_keyword(start, end, "else", TOKEN_KEYWORD_ELSE);
        case 'w':
            return check_keyword(start, end, "while", TOKEN_KEYWORD_WHILE);
        case 'v':
            return check_keyword(start, end, "var", TOKEN_KEYWORD_VAR);
    }

    return TOKEN_IDENTIFIER;
}

static Token lex(Parser* p) {
    if (p->lexer_cache.start) {
        Token cache = p->lexer_cache;
        p->lexer_cache.start = 0;
        return cache;
    }

    while (isspace(*p->lexer_char)) {
        if (*p->lexer_char == '\n') {
            ++p->lexer_line;
        }

        ++p->lexer_char;
    }

    char* start = p->lexer_char++;
    int line = p->lexer_line;
    int kind = *start;

    switch (start[0]) {
        default:
            if (isdigit(start[0])) {
                while (isdigit(*p->lexer_char)) {
                    p->lexer_char++;
                }

                kind = TOKEN_INT_LITERAL;
            }
            else if (isident(start[0])) {
                while (isident(*p->lexer_char)) {
                    p->lexer_char++;
                }

                kind = identifier_kind(start, p->lexer_char);
            }
            break;

        case '\0':
            --p->lexer_char;
            kind = TOKEN_EOF;
            break;
    }

    return (Token) {
        .kind = kind,
        .length = (int)(p->lexer_char - start),
        .start = start,
        .line = line
    };
}

static Token peek(Parser* p) {
    if (!p->lexer_cache.start) {
        p->lexer_cache = lex(p);
    }

    return p->lexer_cache;
}

static void error_at_token(Parser* p, Token token, char* format, ...) {
    char* line_start = token.start;
    while (line_start != p->source && *line_start != '\n') {
        --line_start;
    }

    while (isspace(*line_start)) {
        ++line_start;
    }

    int line_length = 0;
    while (line_start[line_length] != '\0' && line_start[line_length] != '\n') {
        ++line_length;
    }

    int offset = printf("%s(%d): error: ", p->source_path, token.line);
    printf("%.*s\n", line_length, line_start);

    offset += (int)(token.start - line_start);
    printf("%*s^ ", offset, "");

    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);

    printf("\n");
}

static String extract_string(Arena* arena, Token token) {
    char* result = arena_push(arena, token.length + 1);

    memcpy(result, token.start, token.length);
    result[token.length] = '\0';

    return (String) {
        .length = token.length,
        .data = result
    };
}

static String token_string_view(Token token) {
    return (String) {
        .data = token.start,
        .length = token.length
    };
}

static bool match(Parser* p, int kind, char* description) {
    Token token = peek(p);

    if (token.kind == kind) {
        lex(p);
        return true;
    }

    error_at_token(p, token, "expected %s", description);
    return false;
}

#define REQUIRE(p, kind, description) do { if(!match(p, kind, description)) { return 0; } } while (false)

static HIR_Node* make_node(Parser* p, HIR_Block* block, HIR_OpCode op, int in_count, int data_size, Token token) {
    HIR_Node* result = arena_type(p->arena, HIR_Node);
    result->token = token;
    result->op = op;
    result->in_count = in_count;
    result->ins = arena_array(p->arena, HIR_Node*, in_count);
    result->data = arena_push(p->arena, data_size);

    if (block) {
        hir_append(block, result);
    }

    return result;
}

typedef struct {
    int count;
    int capacity;

    String* keys;
    HIR_Node** values;
} SymbolTable;

static void add_symbol_static(int capacity, String* keys, HIR_Node** values, HIR_Node* symbol, String name) {
    int i = fnv1a_hash(name.data, name.length) % capacity;

    for (int j = 0; j < capacity; ++j) {
        if (!keys[i].data) {
            keys[i] = name;
            values[i] = symbol;
            return;
        }

        assert(!strings_identical(keys[i], name));

        i = (i + 1) % capacity;
    }

    assert(false);
}

static void add_symbol(SymbolTable* table, HIR_Node* symbol, String name) {
    if (!table->capacity || load_factor(table->count, table->capacity) > 0.5f)
    {
        int new_capacity = table->capacity ? table->capacity * 2 : 8;
        String* new_keys = calloc(new_capacity, sizeof(String));
        HIR_Node** new_values = calloc(new_capacity, sizeof(HIR_Node*));

        for (int i = 0; i < table->capacity; ++i) {
            if (table->keys[i].data) {
                add_symbol_static(new_capacity, new_keys, new_values, table->values[i], table->keys[i]);
            }
        }

        free(table->keys);
        free(table->values);

        table->capacity = new_capacity;
        table->keys = new_keys;
        table->values = new_values;
    }

    add_symbol_static(table->capacity, table->keys, table->values, symbol, name);
    table->count++;
}

static HIR_Node* find_symbol_in_table(SymbolTable* table, String name) {
    if (!table->capacity) {
        return 0;
    }

    int i = fnv1a_hash(name.data, name.length) % table->capacity;

    for (int j = 0; j < table->capacity; ++j) {
        if (!table->keys[i].data) {
            return 0;
        }

        if (strings_identical(table->keys[i], name)) {
            return table->values[i];
        }

        i = (i + 1) % table->capacity;
    }

    return 0;
}

static void free_symbol_table(SymbolTable* table) {
    free(table->keys);
    free(table->values);
    memset(table, 0, sizeof(*table));
}

typedef struct Scope Scope;
struct Scope {
    Scope* outer;
    SymbolTable table;
};

static HIR_Node* find_symbol(Scope* scope, String name) {
    HIR_Node* result = find_symbol_in_table(&scope->table, name);

    if (result) {
        return result;
    }

    if (scope->outer) {
        return find_symbol(scope->outer, name);
    }

    return 0;
}

static HIR_Node* parse_primary(Parser* p, HIR_Block** block, Scope* scope) {
    (void)block;

    Token token = peek(p);

    switch (token.kind) {
        case TOKEN_INT_LITERAL: {
            lex(p);

            int value = 0;

            for (int i = 0; i < token.length; ++i) {
                value *= 10;
                value += token.start[i] - '0';
            }

            HIR_Node* result = make_node(p, *block, HIR_OP_INTEGER_LITERAL, 0, sizeof(int), token);
            *(int*)result->data = value;

            return result;
        } break;

        case TOKEN_IDENTIFIER: {
            lex(p);

            HIR_Node* var = find_symbol(scope, token_string_view(token));
            if (!var) {
                error_at_token(p, token, "symbol does not exist in the current scope");
                return 0;
            }

            HIR_Node* result = make_node(p, *block, HIR_OP_LOAD, 1, 0, token);
            result->ins[0] = var;

            return result;
        } break;
    }

    error_at_token(p, token, "expected an expression");
    return 0;
}

static int binary_precedence(Token operator) {
    switch (operator.kind) {
        default:
            return 0;
        case '*':
        case '/':
            return 20;
        case '+':
        case '-':
            return 10;
    }
}

static HIR_OpCode binary_operator(Token operator) {
    switch (operator.kind) {
        default:
            assert(false);
            return HIR_OP_ILLEGAL;
        case '*':
            return HIR_OP_MUL;
        case '/':
            return HIR_OP_DIV;
        case '+':
            return HIR_OP_ADD;
        case '-':
            return HIR_OP_SUB;
    }
}

static HIR_Node* parse_binary(Parser* p, HIR_Block** block, Scope* scope, int caller_precedence) {
    HIR_Node* left = parse_primary(p, block, scope);
    if (!left) {
        return 0;
    }

    while (binary_precedence(peek(p)) > caller_precedence) {
        Token operator = lex(p);

        HIR_Node* right = parse_binary(p, block, scope, binary_precedence(operator));
        if (!right) {
            return 0;
        }

        HIR_Node* result = make_node(p, *block, binary_operator(operator), 2, 0, operator);
        result->ins[0] = left;
        result->ins[1] = right;

        left = result;
    }

    return left;
}

static HIR_Node* address_of(Parser* parser, HIR_Node* node) {
    switch (node->op) {
        case HIR_OP_LOAD: {
            hir_remove(node);
            return node->ins[0];
        } break;
    }

    error_at_token(parser, node->token, "cannot assign this expression");
    return 0;
}

static HIR_Node* parse_assign(Parser* p, HIR_Block** block, Scope* scope) {
    HIR_Node* left = parse_binary(p, block, scope, 0);
    if (!left) {
        return 0;
    }

    if (peek(p).kind == '=') {
        Token equals = lex(p);

        HIR_Node* right = parse_assign(p, block, scope);
        if (!right) {
            return 0;
        }

        HIR_Node* lvalue = address_of(p, left);
        if (!lvalue) {
            return 0;
        }

        HIR_Node* result = make_node(p, *block, HIR_OP_ASSIGN, 2, 0, equals);
        result->ins[0] = lvalue;
        result->ins[1] = right;

        return right;
    }

    return left;
}

static HIR_Node* parse_expression(Parser* p, HIR_Block** block, Scope* scope) {
    return parse_assign(p, block, scope);
}

static bool until(Parser* p, int kind) {
    return peek(p).kind != kind && peek(p).kind != TOKEN_EOF;
}

static bool parse_statement(Parser* p, HIR_Block** block, Scope* scope);

static bool parse_block(Parser* p, HIR_Block** block, Scope* scope) {
    bool result = true;

    REQUIRE(p, '{', "{");

    Scope inner = {
        .outer = scope,
    };

    while (until(p, '}')) {
        if (!parse_statement(p, block, &inner)) {
            result = false;
            goto exit;
        }
    }

    Token rbrace = peek(p);

    if(!match(p, '}', "}")) {
        result = false;
        goto exit;
    }

    p->last_rbrace = rbrace;

    exit:
    free_symbol_table(&inner.table);
    return result;
}

static void jump(Parser* p, HIR_Block* from, HIR_Block* to, Token token) {
    HIR_Node* jmp = make_node(p, from, HIR_OP_JUMP, 0, sizeof(HIR_Block*), token);
    *(HIR_Block**)jmp->data = to;
}

static void branch(Parser* p, HIR_Block* from, HIR_Node* predicate, HIR_Block* head_true, HIR_Block* head_false, Token token) {
    HIR_Node* br = make_node(p, from, HIR_OP_BRANCH, 1, 2 * sizeof(HIR_Block*), token);
    br->ins[0] = predicate;
    HIR_Block** array = br->data;
    array[0] = head_true;
    array[1] = head_false;
}

static bool parse_statement(Parser* p, HIR_Block** block, Scope* scope) {
    Token token = peek(p);

    switch (token.kind) {
        default: {
            if (!parse_expression(p, block, scope)) {
                return false;
            }

            REQUIRE(p, ';', ";");

            return true;
        } break;

        case '{':
            return parse_block(p, block, scope);

        case TOKEN_KEYWORD_RETURN: {
            REQUIRE(p, TOKEN_KEYWORD_RETURN, "return");

            HIR_Node* expression = parse_expression(p, block, scope);
            if (!expression) {
                return false;
            }

            REQUIRE(p, ';', ";");

            HIR_Node* node = make_node(p, *block, HIR_OP_RETURN, 1, 0, token);
            node->ins[0] = expression;

            HIR_Block* tail = make_block(p);
            *block = tail;

            return true;
        } break;

        case TOKEN_KEYWORD_IF: {
            REQUIRE(p, TOKEN_KEYWORD_IF, "if");

            HIR_Node* predicate = parse_expression(p, block, scope);

            HIR_Block* head_true = make_block(p);
            HIR_Block* tail_true = head_true;

            if(!parse_block(p, &tail_true, scope)) {
                return false;
            }

            Token true_block_rbrace = p->last_rbrace;

            HIR_Block* head_false = make_block(p);
            HIR_Block* end = head_false;

            if (peek(p).kind == TOKEN_KEYWORD_ELSE) {
                lex(p);

                HIR_Block* tail_false = head_false;
                if (!parse_block(p, &tail_false, scope)) {
                    return false;
                }

                end = make_block(p);
                jump(p, tail_false, end, p->last_rbrace);
            }

            branch(p, *block, predicate, head_true, head_false, token);
            jump(p, tail_true, end, true_block_rbrace);
            *block = end;

            return true;
        } break;

        case TOKEN_KEYWORD_WHILE: {
            REQUIRE(p, TOKEN_KEYWORD_WHILE, "while");

            HIR_Block* head_start = make_block(p);
            HIR_Block* tail_start = head_start;

            HIR_Node* predicate = parse_expression(p, &tail_start, scope);
            if (!predicate) {
                return false;
            }

            HIR_Block* head_body = make_block(p);
            HIR_Block* tail_body = head_body;

            if (!parse_block(p, &tail_body, scope)) {
                return false;
            }

            HIR_Block* end = make_block(p);

            jump(p, *block, head_start, token);
            branch(p, tail_start, predicate, head_body, end, token);
            jump(p, tail_body, head_start, p->last_rbrace);

            *block = end;

            return true;
        } break;

        case TOKEN_KEYWORD_VAR: {
            REQUIRE(p, TOKEN_KEYWORD_VAR, "var");

            Token name = peek(p);
            REQUIRE(p, TOKEN_IDENTIFIER, "an identifier");

            REQUIRE(p, ';', ";");

            if (find_symbol(scope, token_string_view(name))) {
                error_at_token(p, name, "this symbol already exists in the current scope");
                return false;
            }

            HIR_Node* node = make_node(p, *block, HIR_OP_VAR, 0, sizeof(String), token);
            *(String*)node->data = extract_string(p->arena, name);

            add_symbol(&scope->table, node, token_string_view(name));

            return true;
        } break;
    }
}

HIR_Proc* parse(Arena* arena, char* source_path, char* source) {
    Parser p = {
        .arena = arena,
        .source_path = source_path,
        .source = source,

        .lexer_char = source,
        .lexer_line = 1
    };

    HIR_Block* control_flow_head = make_block(&p);
    HIR_Block* control_flow_tail = control_flow_head;

    if (!parse_block(&p, &control_flow_tail, 0)) {
        return 0;
    }

    HIR_Proc* proc = arena_type(arena, HIR_Proc);
    proc->control_flow_head = control_flow_head;

    return proc;
}