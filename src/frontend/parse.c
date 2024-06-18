#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "frontend.h"

typedef struct {
    Arena* arena;
    char* source_path;
    char* source;

    char* lexer_char;
    int lexer_line;
    Token lexer_cache;

    HIR_Block* control_flow_tail;
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

static HIR_Node* make_node(Parser* p, HIR_Block* block, HIR_OpCode op, int in_count, int data_size) {
    HIR_Node* result = arena_type(p->arena, HIR_Node);
    result->op = op;
    result->in_count = in_count;
    result->ins = arena_array(p->arena, HIR_Node*, in_count);
    result->data = arena_push(p->arena, data_size);

    if (block) {
        hir_append(block, result);
    }

    return result;
}

static HIR_Node* parse_primary(Parser* p, HIR_Block** block) {
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

            HIR_Node* result = make_node(p, *block, HIR_OP_INTEGER_LITERAL, 0, sizeof(int));
            *(int*)result->data = value;

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

static HIR_Node* parse_binary(Parser* p, HIR_Block** block, int caller_precedence) {
    HIR_Node* left = parse_primary(p, block);
    if (!left) {
        return 0;
    }

    while (binary_precedence(peek(p)) > caller_precedence) {
        Token operator = lex(p);

        HIR_Node* right = parse_binary(p, block, binary_precedence(operator));
        if (!right) {
            return 0;
        }

        HIR_Node* result = make_node(p, *block, binary_operator(operator), 2, 0);
        result->ins[0] = left;
        result->ins[1] = right;

        left = result;
    }

    return left;
}

static HIR_Node* parse_expression(Parser* p, HIR_Block** block) {
    return parse_binary(p, block, 0);
}

static bool until(Parser* p, int kind) {
    return peek(p).kind != kind && peek(p).kind != TOKEN_EOF;
}

static bool parse_statement(Parser* p, HIR_Block** block);

static bool parse_block(Parser* p, HIR_Block** block) {
    REQUIRE(p, '{', "{");

    while (until(p, '}')) {
        if (!parse_statement(p, block)) {
            return false;
        }
    }

    REQUIRE(p, '}', "}");

    return true;
}

static bool parse_statement(Parser* p, HIR_Block** block) {
    Token token = peek(p);

    switch (token.kind) {
        default: {
            if (!parse_expression(p, block)) {
                return false;
            }

            REQUIRE(p, ';', ";");

            return true;
        } break;

        case '{':
            return parse_block(p, block);

        case TOKEN_KEYWORD_RETURN: {
            REQUIRE(p, TOKEN_KEYWORD_RETURN, "return");

            HIR_Node* expression = parse_expression(p, block);
            if (!expression) {
                return false;
            }

            REQUIRE(p, ';', ";");

            HIR_Node* node = make_node(p, *block, HIR_OP_RETURN, 1, 0);
            node->ins[0] = expression;

            HIR_Block* tail = make_block(p);
            *block = tail;

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

    if (!parse_block(&p, &control_flow_tail)) {
        return 0;
    }

    HIR_Proc* proc = arena_type(arena, HIR_Proc);
    proc->control_flow_head = control_flow_head;

    return proc;
}