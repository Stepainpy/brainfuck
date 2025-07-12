/* Prefix cheat sheet
 * bf* -> brainfuck *
 *   s -> stack
 *   c -> code
 *   p -> parse
 * I i -> instruction
 *   M -> mask
 *   C -> constant
 *   K -> kind
 *   I -> instruction
 */

/* Brainfuck instruction set
 * .-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 * |F|E|D|C|B|A|9|8|7|6|5|4|3|2|1|0| - bit index
 * '-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *
 * .-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 * |0 0|       signed delta        | - cell modification
 * '-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 * .-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 * |0 1|       signed offset       | - moving cursor
 * '-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 * .-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 * |1 0|Z|L|       distance        | - jump if zero (Z = 0) or if nonzero (Z = 1)
 * '-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *      -+-+-+-+-+-+-+-+-+-+-+-+-+-.
 *       |0|MSB                 LSB| - short jump (12 bit, 1 instruction)
 *      -+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *      -+-+-+ .. +-. .-+-+ .. +-+-.
 *       |1|   Hi   | |     Lo     | - long jump (28 bit, 2 instructions)
 *      -+-+-+ .. +-' '-+-+ .. +-+-'
 * .-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 * |1 1|A|* * * * * * * * * * * * *| - extension operations
 * '-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *    -+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 *     |0|* * * * * * * * * * * * *| - instruction within value
 *    -+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
 *     |1|* * * * *|      arg      | - instruction without value
 *    -+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 */

#include "brainfuck.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PSTR(x) # x
#define  STR(x) PSTR(x)
#define INVAL_INDEX ((size_t)-1)
#define PAREN_STACK_DEPTH 1023
#define NBIT_MAX(bitcount) ((1 << (bitcount)) - 1)

typedef uint16_t bft_instr;

struct bft_program {
    size_t instr_count;
    bft_instr* instrs;
    bft_cell* memory;
};

typedef struct {
    bft_instr* items;
    size_t count, capacity;
} bft_code;

static struct bft_paren_stack {
    size_t head, positions[PAREN_STACK_DEPTH];
} paren_stack;

enum {
    BFM_16BIT = NBIT_MAX(16),
    BFM_14BIT = NBIT_MAX(14),
    BFM_12BIT = NBIT_MAX(12),
    BFM_08BIT = NBIT_MAX( 8),
    BFM_EX_ARG = BFM_08BIT,
};

enum {
    BFM_KIND_2BIT = 0xC000,
    BFM_KIND_3BIT = 0xE000,
    BFM_KIND_8BIT = 0xFF00,
};

enum {
    BFC_MAX_MEMORY = 1 << 15, // 32 KiB
    BFC_MAX_MEMORY_BYTES = BFC_MAX_MEMORY * sizeof(bft_cell),
    BFC_MAX_JUMP_SH_DIST = NBIT_MAX(12),
    BFC_MAX_JUMP_LO_DIST = NBIT_MAX(28),
    BFC_EX_ARG_MAX = NBIT_MAX(8),
    BFC_S14BIT_MIN = -8192,
    BFC_S14BIT_MAX =  8191,
};

enum {
    BFI_CHG = 0 << 14,
        BFK_INC = BFI_CHG | 0 << 13,
        BFK_DEC = BFI_CHG | 1 << 13,
    BFI_MOV = 1 << 14,
        BFK_MOV_RT = BFI_MOV | 0 << 13,
        BFK_MOV_LT = BFI_MOV | 1 << 13,
    BFK_JMP = 2 << 14,
        BFK_JMP_IS_LONG  =  1 << 12,
        BFI_JZ  = BFK_JMP | 0 << 13,
        BFI_JNZ = BFK_JMP | 1 << 13,
    BFK_EXT = 3 << 14,
        BFK_EXT_IM = BFK_EXT | 0 << 13,
            BFI_IO_INPUT = BFK_EXT_IM,
            BFI_MEMSET_ZERO,
            BFI_MOV_RT_UNTIL_ZERO,
            BFI_MOV_LT_UNTIL_ZERO,
            // halt instruction has code value is 0xDEAD
            BFI_DEAD = BFK_EXT_IM | 0x1EAD,
        BFK_EXT_EX = BFK_EXT | 1 << 13,
            BFI_OUTNTIMES = BFK_EXT_EX | 0 << 8,
};

static void bfs_init(struct bft_paren_stack* stack) { stack->head = 0; }

static bft_error bfs_push(struct bft_paren_stack* stack, size_t pos) {
    if (stack->head >= PAREN_STACK_DEPTH)
        return BFE_STACK_OVERFLOW;
    stack->positions[stack->head++] = pos;
    return BFE_OK;
}

static size_t bfs_pop(struct bft_paren_stack* stack) {
    if (stack->head == 0) return INVAL_INDEX;
    return stack->positions[--stack->head];
}

static bft_error bfc_reserve(bft_code* code) {
    if (code->count < code->capacity) return BFE_OK;
    code->capacity += code->capacity == 0 ? 64 : code->capacity / 2;
    code->items = realloc(code->items, code->capacity * sizeof *code->items);
    return code->items ? BFE_OK : BFE_NO_MEMORY;
}

static bft_error bfc_push(bft_code* code, bft_instr instr) {
    if (bfc_reserve(code)) return BFE_NO_MEMORY;
    code->items[code->count++] = instr;
    return BFE_OK;
}

static bft_error bfc_insert(bft_code* code, bft_instr instr, size_t pos) {
    if (bfc_reserve(code)) return BFE_NO_MEMORY;
    memmove(code->items + pos + 1, code->items + pos,
        (code->count - pos) * sizeof *code->items);
    code->items[pos] = instr; ++code->count;
    return BFE_OK;
}

void bfa_destroy(bft_program* prog) {
    if (!prog) return;
    free(prog->instrs);
    free(prog->memory);
    free(prog);
}

const char* bfa_strerror(bft_error error) {
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (error) {
        default: return "unknown error";
        case BFE_OK: return "no errors";
        case BFE_UNREACHABLE: return "return from unreachable point";
        case BFE_NULL_POINTER: return "null pointer passed";
        case BFE_NO_MEMORY: return "there is no memory to allocate";
        case BFE_STACK_OVERFLOW: return "the maximum stack depth for"
            " brackets has been reached (limit is " STR(PAREN_STACK_DEPTH) ")";
        case BFE_UNBALANCED_BRACKETS: return "unbalanced brackets";
        case BFE_VERY_LONG_JUMP: return "the relative jump is too long";
        case BFE_INVALID_ENV: return "invalid values in environment";
        case BFE_UNKNOWN_INSTR: return "unknown instruction";
        case BFE_MEMORY_CORRUPTION: return "memory corruption";
    }
#pragma GCC diagnostic pop
}

static struct s14bit_t { int16_t x : 14; } s14bit;
#define bf_sign_extend_14(integer) (int64_t)(s14bit.x = (integer) & BFM_14BIT)

#define bf_throw(rc_) do { \
    rc = rc_; goto error;  \
} while (0)

#define bfi_last(src) src->items[src->count - 1]

#define bfi_push(dest, instr) do { \
    if (bfc_push(dest, instr)) \
        bf_throw(BFE_NO_MEMORY); \
} while (0)

#define bfi_insert(dest, instr, pos) do { \
    if (bfc_insert(dest, instr, pos)) \
        bf_throw(BFE_NO_MEMORY); \
} while (0)

static bool bfi_prev_is(bft_code* code, int type) {
    return code->count && (bfi_last(code) & BFM_KIND_2BIT) == type;
}

static bool bfp_is_oper(char ch) {
    return ch == ',' || ch == '.' || ch == '+' || ch == '-'
        || ch == '>' || ch == '<' || ch == '[' || ch == ']';
}

static const char* bfp_next_oper(const char* ptr, const char* end) {
    while (ptr < end && !bfp_is_oper(*ptr)) ++ptr;
    return ptr;
}

static const char* bfp_skip_n_opers(const char* ptr, const char* end, size_t count) {
    while (ptr < end && count > 0)
        if (bfp_is_oper(*ptr++)) --count;
    return ptr;
}

static bool bfp_has_pattern(const char* ptr, const char* end, const char* pattern) {
    ptr = bfp_next_oper(ptr, end);
    while (ptr < end && *ptr == *pattern) {
        ptr = bfp_next_oper(ptr + 1, end);
        ++pattern;
    }
    return *pattern == '\0';
}

static const char* bfp_collapse_opers(
    const char* ptr, const char* end,
    struct s14bit_t* acc, char inc, char dec
) {
    ptr = bfp_next_oper(ptr, end);
    while (ptr < end) {
        /*  */ if (*ptr == inc) {
            if (acc->x < BFC_S14BIT_MAX) ++acc->x; else return ptr;
        } else if (*ptr == dec) {
            if (acc->x > BFC_S14BIT_MIN) --acc->x; else return ptr;
        } else return ptr;
        ptr = bfp_next_oper(ptr + 1, end);
    }
    return ptr;
}

static bft_error bfp_collapse_instr(bft_code* code, int type, struct s14bit_t cur_acc) {
    bft_error rc = BFE_OK;
    /**/ if (cur_acc.x == 0) return rc;
    else if (bfi_prev_is(code, type)) {
        int32_t prev_acc = bf_sign_extend_14(bfi_last(code));
        /*  */ if ((prev_acc < 0 && cur_acc.x > 0) || (prev_acc > 0 && cur_acc.x < 0)) {
            struct s14bit_t new_acc = { cur_acc.x + prev_acc };
            if (new_acc.x == 0) { --code->count; return rc; }
            bfi_last(code) = type | (new_acc.x & BFM_14BIT);
        } else if ((prev_acc < 0 && cur_acc.x < 0) || (prev_acc > 0 && cur_acc.x > 0)) {
            int32_t new_acc = prev_acc + cur_acc.x;
            /*  */ if (new_acc < BFC_S14BIT_MIN) {
                bfi_last(code) = type | (BFC_S14BIT_MIN & BFM_14BIT);
                bfi_push(code, type | ((new_acc - BFC_S14BIT_MIN) & BFM_14BIT));
            } else if (new_acc > BFC_S14BIT_MAX) {
                bfi_last(code) = type | (BFC_S14BIT_MAX & BFM_14BIT);
                bfi_push(code, type | ((new_acc - BFC_S14BIT_MAX) & BFM_14BIT));
            } else
                bfi_last(code) = type | (new_acc & BFM_14BIT);
        } else
            return BFE_UNREACHABLE;
    } else
        bfi_push(code, type | (cur_acc.x & BFM_14BIT));
error:
    return rc;
}

bft_error bfa_compile(bft_program** prog, const char* src, size_t size) {
    if (!prog || (!src && size > 0))
        return BFE_NULL_POINTER;
    bft_error rc = BFE_OK;
    bft_code code[1] = {0};
    bfs_init(&paren_stack);

    void* rt_memory = *prog = NULL;
    const char* end = src + size;
    char ch, inc, dec;

    while (src < end) {
        switch ((ch = *src++)) {
            case ',': bfi_push(code, BFI_IO_INPUT); break;
            case '.': {
                int count = 0;
                src = bfp_next_oper(src, end);
                while (src < end) {
                    if (*src != '.' || count >= BFC_EX_ARG_MAX) break;
                    ++count; src = bfp_next_oper(src + 1, end);
                }
                bfi_push(code, BFI_OUTNTIMES | count);
            } break;
            case '+': case '-': case '>': case '<': {
                /**/ if (ch == '+' || ch == '-') inc = '+', dec = '-';
                else if (ch == '>' || ch == '<') inc = '>', dec = '<';
                struct s14bit_t acc = { ch == inc ? 1 : -1 };
                src = bfp_collapse_opers(src, end, &acc, inc, dec);
                rc  = bfp_collapse_instr(code, inc == '+' ? BFI_CHG : BFI_MOV, acc);
                if (rc) goto error;
            } break;
            case '[': {
                /*  */ if (bfp_has_pattern(src, end, "-]")
                        || bfp_has_pattern(src, end, "+]")) {
                    bfi_push(code, BFI_MEMSET_ZERO);
                    src = bfp_skip_n_opers(src, end, 2);
                } else if (bfp_has_pattern(src, end, ">]")) {
                    bfi_push(code, BFI_MOV_RT_UNTIL_ZERO);
                    src = bfp_skip_n_opers(src, end, 2);
                } else if (bfp_has_pattern(src, end, "<]")) {
                    bfi_push(code, BFI_MOV_LT_UNTIL_ZERO);
                    src = bfp_skip_n_opers(src, end, 2);
                } else {
                    if (bfs_push(&paren_stack, code->count))
                        bf_throw(BFE_STACK_OVERFLOW);
                    bfi_push(code, BFI_JZ); // placeholder
                }
            } break;
            case ']': {
                size_t pos = bfs_pop(&paren_stack);
                if (pos == INVAL_INDEX)
                    bf_throw(BFE_UNBALANCED_BRACKETS);

                size_t dist = code->count - pos;
                /*  */ if (dist > BFC_MAX_JUMP_LO_DIST) {
                    bf_throw(BFE_VERY_LONG_JUMP);
                } else if (dist > BFC_MAX_JUMP_SH_DIST) {
                    code->items[pos] = BFI_JZ | BFK_JMP_IS_LONG | (dist >> 16);
                    bfi_insert(code, dist & BFM_16BIT, pos + 1);
                    bfi_push(code, BFI_JNZ | BFK_JMP_IS_LONG | (dist >> 16));
                    bfi_push(code, dist & BFM_16BIT);
                } else {
                    code->items[pos] = BFI_JZ | dist;
                    bfi_push(code, BFI_JNZ | dist);
                }
            } break;
        }
    }

    if (paren_stack.head > 0)
        bf_throw(BFE_UNBALANCED_BRACKETS);
    bfi_push(code, BFI_DEAD);

    *prog = malloc(sizeof **prog);
    if (!*prog) bf_throw(BFE_NO_MEMORY);

    rt_memory = calloc(1, BFC_MAX_MEMORY_BYTES);
    if (!rt_memory) bf_throw(BFE_NO_MEMORY);

    (*prog)->instr_count = code->count - 1;
    (*prog)->instrs = code->items;
    (*prog)->memory = rt_memory;

    return BFE_OK;
error:
    bfa_destroy(*prog);
    return rc;
}

bft_error bfa_execute(bft_program* prog, bft_env* env) {
    if (!prog || !env) return BFE_NULL_POINTER;
    if (!env->input || !env->output || !env->read || !env->write)
        return BFE_INVALID_ENV;

    // program and memory counter
    size_t pc = 0, mc = 0;

    while (true) {
        bft_instr instr = prog->instrs[pc];
        switch (instr & BFM_KIND_2BIT) {
            case BFI_CHG: prog->memory[mc] += bf_sign_extend_14(instr); break;
            case BFI_MOV:
                mc += bf_sign_extend_14(instr);
                if (mc >= BFC_MAX_MEMORY) return BFE_MEMORY_CORRUPTION;
                break;
            case BFK_JMP: {
                size_t dist = instr & BFM_12BIT;
                if (instr & BFK_JMP_IS_LONG)
                    dist = (dist << 16) + prog->instrs[++pc] + 1;
                /**/ if (prog->memory[mc] == 0 && (instr & BFM_KIND_3BIT) == BFI_JZ ) pc += dist;
                else if (prog->memory[mc] != 0 && (instr & BFM_KIND_3BIT) == BFI_JNZ) pc -= dist;
            } break;
            case BFK_EXT:
                /**/ if ((instr & BFM_KIND_3BIT) == BFK_EXT_IM)
                    switch (instr) {
                        case BFI_DEAD: return BFE_OK;
                        case BFI_IO_INPUT: prog->memory[mc] = env->read(env->input); break;
                        case BFI_MEMSET_ZERO: prog->memory[mc] = 0; break;
                        case BFI_MOV_RT_UNTIL_ZERO: {
                            bft_cell* last_cell = prog->memory + BFC_MAX_MEMORY - 1;
                            bft_cell* zero = prog->memory + mc;
                            while (zero < last_cell && *zero != 0) ++zero;
                            if (*zero) return BFE_MEMORY_CORRUPTION;
                            mc = zero - prog->memory;
                        } break;
                        case BFI_MOV_LT_UNTIL_ZERO: {
                            bft_cell* zero = prog->memory + mc;
                            while (prog->memory < zero && *zero != 0) --zero;
                            if (*zero) return BFE_MEMORY_CORRUPTION;
                            mc = zero - prog->memory;
                        } break;
                        default: return BFE_UNKNOWN_INSTR;
                    }
                else if ((instr & BFM_KIND_3BIT) == BFK_EXT_EX)
                    switch (instr & BFM_KIND_8BIT) {
                        case BFI_OUTNTIMES:
                            for (size_t i = 0; i <= (instr & BFM_EX_ARG); i++)
                                env->write(prog->memory[mc], env->output);
                            break;
                        default: return BFE_UNKNOWN_INSTR;
                    }
                break;
        }
        ++pc;
    }

    return BFE_UNREACHABLE;
}

void bfd_instrs_dump_txt(bft_program* prog, FILE* dest, size_t limit) {
    if (!prog) return;

    const int addr_width = floor(log10(prog->instr_count)) + 1;
    bft_instr* instr = prog->instrs;
    for (size_t i = 0; i < limit && *instr != BFI_DEAD; i++, instr++) {
        fprintf(dest, "[%*zu]: %04hx - ", addr_width, i, *instr);
        switch (*instr & BFM_KIND_3BIT) {
            case BFK_INC: fprintf(dest, "increment by %lli",  bf_sign_extend_14(*instr)); break;
            case BFK_DEC: fprintf(dest, "decrement by %lli", -bf_sign_extend_14(*instr)); break;
            case BFK_MOV_RT: fprintf(dest, "move rigth by %lli",  bf_sign_extend_14(*instr)); break;
            case BFK_MOV_LT: fprintf(dest, "move left  by %lli", -bf_sign_extend_14(*instr)); break;
            case BFI_JZ:
                if (*instr & BFK_JMP_IS_LONG) {
                    size_t dist = ((instr[0] & BFM_12BIT) << 16) + instr[1] + 1;
                    fprintf(dest, "jump ahead by %zu instructions (on %zu)\n", dist, i + dist);
                    fprintf(dest, "[%*zu]: %04hx", addr_width, ++i, *(++instr));
                } else
                    fprintf(dest, "jump ahead by %u instructions (on %zu)",
                        *instr & BFM_12BIT, i + (*instr & BFM_12BIT));
                break;
            case BFI_JNZ:
                if (*instr & BFK_JMP_IS_LONG) {
                    size_t dist = ((instr[0] & BFM_12BIT) << 16) + instr[1] + 1;
                    fprintf(dest, "jump back %zu instructions (on %zu)\n", dist, i - dist);
                    fprintf(dest, "[%*zu]: %04hx", addr_width, ++i, *(++instr));
                } else
                    fprintf(dest, "jump back %u instructions (on %zu)",
                        *instr & BFM_12BIT, i - (*instr & BFM_12BIT));
                break;
            case BFK_EXT_IM:
                switch (*instr) {
                    case BFI_IO_INPUT: fprintf(dest, "input character"); break;
                    case BFI_MEMSET_ZERO: fprintf(dest, "set zero value"); break;
                    case BFI_MOV_RT_UNTIL_ZERO: fprintf(dest, "move to right until it's zero"); break;
                    case BFI_MOV_LT_UNTIL_ZERO: fprintf(dest, "move to left  until it's zero"); break;
                    default: fprintf(dest, "unknown instruction"); break;
                } break;
            case BFK_EXT_EX:
                switch (*instr & BFM_KIND_8BIT) {
                    case BFI_OUTNTIMES: {
                        uint8_t count = *instr & BFM_EX_ARG;
                        fprintf(dest, "output character");
                        if (count) fprintf(dest, " %hhu times", count + 1);
                    } break;
                    default: fprintf(dest, "unknown instruction"); break;
                } break;
        }
        fputc('\n', dest);
    }

    if (*instr != BFI_DEAD)
        fprintf(dest, "...\n");
}

void bfd_memory_dump_txt(bft_program* prog, FILE* dest, size_t offset, size_t size) {
    size_t min_size = size < (BFC_MAX_MEMORY - offset) ? size : (BFC_MAX_MEMORY - offset);
    uint8_t buffer[32]; size_t rdlen;
    do {
        rdlen = (min_size >= sizeof buffer) ? sizeof buffer : min_size;
        if (rdlen == 0) break;

        memcpy(buffer, prog->memory + offset, rdlen);
        offset   += rdlen;
        min_size -= rdlen;

        for (size_t i = 1; i <= rdlen; i++)
            fprintf(dest, "%02hhx%*s", buffer[i-1], i % 8 ? 1 : 2, "");
        fputc('\n', dest);
    } while (rdlen == sizeof buffer);
}

void bfd_memory_dump_bin(bft_program* prog, FILE* dest, size_t offset, size_t size) {
    size_t min_size = size < BFC_MAX_MEMORY - offset ? size : BFC_MAX_MEMORY - offset;
    fwrite(prog->memory + offset, 1, min_size, dest);
}