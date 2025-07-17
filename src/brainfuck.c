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

/* Brainfuck interpreter instruction set
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
 *       |0|                       | - short jump (12 bit, 1 instruction)
 *      -+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *      -+-+-+ ... +-. .-+- ... -+-.
 *       |1|         | |           | - long jump (28 bit, 2 instructions)
 *      -+-+-+ ... +-' '-+- ... -+-'
 * .-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 * |1 1|A|* * * * * * * * * * * * *| - extension operations
 * '-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *    -+-+-+-+-+-+-+-+-+-+-+-+-+-+-.
 *     |0|          IM-ID          | - instruction without value
 *    -+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
 *     |1|  EX-ID  |      arg      | - instruction within value
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

typedef struct {
    bft_instr* items;
    size_t count, capacity;
} bft_instrs;

static struct bft_paren_stack {
    size_t head, positions[PAREN_STACK_DEPTH];
} paren_stack;

enum {
    BFM_16BIT = NBIT_MAX(16),
    BFM_14BIT = NBIT_MAX(14),
    BFM_12BIT = NBIT_MAX(12),
    BFM_08BIT = NBIT_MAX( 8),
    BFM_EX_ARG = BFM_08BIT,
    BFM_JMP_ZBIT = 0x2000,
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
            BFI_BREAKPOINT,
            // halt instruction has code value is 0xDEAD
            BFI_DEAD = BFK_EXT_IM | 0x1EAD,
        BFK_EXT_EX = BFK_EXT | 1 << 13,
            BFI_OUTNTIMES = BFK_EXT_EX | 0 << 8,
            BFI_DMOV_RT   = BFK_EXT_EX | 1 << 8,
            BFI_DMOV_LT   = BFK_EXT_EX | 2 << 8,
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

static bft_error bfc_reserve(bft_instrs* code) {
    if (code->count < code->capacity) return BFE_OK;
    code->capacity += code->capacity == 0 ? 64 : code->capacity / 2;
    code->items = realloc(code->items, code->capacity * sizeof *code->items);
    return code->items ? BFE_OK : BFE_NO_MEMORY;
}

static bft_error bfc_push(bft_instrs* code, bft_instr instr) {
    if (bfc_reserve(code)) return BFE_NO_MEMORY;
    code->items[code->count++] = instr;
    return BFE_OK;
}

static bft_error bfc_insert(bft_instrs* code, bft_instr instr, size_t pos) {
    if (bfc_reserve(code)) return BFE_NO_MEMORY;
    memmove(code->items + pos + 1, code->items + pos,
        (code->count - pos) * sizeof *code->items);
    code->items[pos] = instr; ++code->count;
    return BFE_OK;
}

void bfa_destroy(bft_program* prog) {
    if (prog) free(prog->items);
}

const char* bfa_strerror(bft_error error) {
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (error) {
        default: return "unknown error";
        case BFE_OK: return "no errors";
        case BFE_BREAKPOINT: return "breakpoint in code";
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

static struct int14_t { int16_t x : 14; } s14bit;
#define bf_sign_extend_14(integer) (int64_t)(s14bit.x = (integer) & BFM_14BIT)

#define bf_abs(x) ((x) < 0 ? -(x) : (x))

#define bf_throw(rc_) do { \
    rc = rc_; goto cleanup; \
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

static bool bfi_prev_is(bft_instrs* code, int type) {
    return code->count && (bfi_last(code) & BFM_KIND_2BIT) == type;
}

static bool bfp_is_oper(char ch) {
    return ch == ',' || ch == '.' || ch == '+' || ch == '-'
        || ch == '>' || ch == '<' || ch == '[' || ch == ']'
        || ch == '@' /* breakpoint symbol */;
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
    struct int14_t* acc, char inc, char dec
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

static bft_error bfp_collapse_instr(bft_instrs* code, int type, struct int14_t cur_acc) {
    bft_error rc = BFE_OK;
    /**/ if (cur_acc.x == 0) return rc;
    else if (bfi_prev_is(code, type)) {
        int32_t prev_acc = bf_sign_extend_14(bfi_last(code));
        /*  */ if ((prev_acc < 0 && cur_acc.x > 0) || (prev_acc > 0 && cur_acc.x < 0)) {
            struct int14_t new_acc = { cur_acc.x + prev_acc };
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
cleanup:
    return rc;
}

// find patterns [-(>|<)*+(<|>)*] and [(>|<)*+(<|>)*-]
static bool bfp_find_data_mov(bft_instrs* code, size_t jz_pos) {
    bft_instr i1, i2, i3, i4;
    i1 = code->items[jz_pos + 1];
    i2 = code->items[jz_pos + 2];
    i3 = code->items[jz_pos + 3];
    i4 = code->items[jz_pos + 4];
    int16_t movn;

    /*  */ if ((i1 & BFM_KIND_3BIT) == BFK_DEC) { // minus 1
        if (bf_sign_extend_14(i1) != -1)     return false;
        if ((i2 & BFM_KIND_2BIT) != BFI_MOV) return false; // mov by n ...
        movn = bf_sign_extend_14(i2);
        if (bf_abs(movn) > BFC_EX_ARG_MAX)   return false; // ... and less 256
        if ((i3 & BFM_KIND_3BIT) != BFK_INC) return false; // plus 1
        if (bf_sign_extend_14(i3) != 1)      return false;
        if ((i4 & BFM_KIND_2BIT) != BFI_MOV) return false; // mov by -n
        if (bf_sign_extend_14(i4) != -movn)  return false;
        /* here is all good */
    } else if ((i1 & BFM_KIND_2BIT) == BFI_MOV) { // mov by n ...
        movn = bf_sign_extend_14(i1);
        if (bf_abs(movn) > BFC_EX_ARG_MAX)   return false; // ... and less 256
        if ((i2 & BFM_KIND_3BIT) != BFK_INC) return false; // plus 1
        if (bf_sign_extend_14(i2) != 1)      return false;
        if ((i3 & BFM_KIND_2BIT) != BFI_MOV) return false; // mov by -n
        if (bf_sign_extend_14(i3) != -movn)  return false;
        if ((i4 & BFM_KIND_3BIT) != BFK_DEC) return false; // minus 1
        if (bf_sign_extend_14(i4) != -1)     return false;
        /* here is all good */
    } else // no pattern starts
        return false;

    code->count = jz_pos + 1;
    code->items[jz_pos] =
        (movn > 0 ? BFI_DMOV_RT : BFI_DMOV_LT) | (bf_abs(movn) & BFM_EX_ARG);
    return true;
}

bft_error bfa_compile(bft_program* prog, const char* src, size_t size) {
    if (!prog || (!src && size > 0))
        return BFE_NULL_POINTER;

    bfs_init(&paren_stack);
    bft_instrs code[1] = {0};
    bft_error rc = BFE_OK;

    const char* end = src + size;
    char ch, inc = '\0', dec = '\0';

    while (src < end) {
        switch ((ch = *src++)) {
            case '@': bfi_push(code, BFI_BREAKPOINT); break;
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
                struct int14_t acc = { ch == inc ? 1 : -1 };
                src = bfp_collapse_opers(src, end, &acc, inc, dec);
                rc  = bfp_collapse_instr(code, inc == '+' ? BFI_CHG : BFI_MOV, acc);
                if (rc) goto cleanup;
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
                    /**/ if (dist == 5 && bfp_find_data_mov(code, pos)) break;
                    else {
                        code->items[pos] = BFI_JZ | dist;
                        bfi_push(code, BFI_JNZ | dist);
                    }
                }
            } break;
        }
    }

    if (paren_stack.head > 0)
        bf_throw(BFE_UNBALANCED_BRACKETS);
    bfi_push(code, BFI_DEAD);

    prog->count = code->count;
    prog->items = realloc(code->items,
        code->count * sizeof *code->items);

    return prog->items ? BFE_OK : BFE_NO_MEMORY;
cleanup:
    free(code->items);
    return rc;
}

bft_error bfa_execute(bft_program* prog, bft_env* env, bft_context* ext_ctx) {
    if (!prog || !env) return BFE_NULL_POINTER;
    if (!env->input || !env->output || !env->read || !env->write)
        return BFE_INVALID_ENV;

    bft_error rc = BFE_OK;
    bft_context ctx = {0};
    if (ext_ctx && ext_ctx->mem)
        ctx = *ext_ctx;
    else {
        ctx.mem = calloc(1, BFC_MAX_MEMORY_BYTES);
        if (!ctx.mem) return BFE_NO_MEMORY;
    }

    while (true) {
        bft_instr instr = prog->items[ctx.pc++];
        switch (instr & BFM_KIND_2BIT) {
            case BFI_CHG: ctx.mem[ctx.mc] += bf_sign_extend_14(instr); break;
            case BFI_MOV:
                ctx.mc += bf_sign_extend_14(instr);
                if (ctx.mc >= BFC_MAX_MEMORY)
                    bf_throw(BFE_MEMORY_CORRUPTION);
                break;
            case BFK_JMP: {
                bool   zbit = instr & BFM_JMP_ZBIT;
                size_t dist = instr & BFM_12BIT;
                if (instr & BFK_JMP_IS_LONG)
                    dist = (dist << 16) + prog->items[ctx.pc++] + 1;
                if ((bool)ctx.mem[ctx.mc] == zbit)
                    ctx.pc += zbit ? -dist : dist;
            } break;
            case BFK_EXT:
                /**/ if ((instr & BFM_KIND_3BIT) == BFK_EXT_IM)
                    switch (instr) {
                        case BFI_DEAD: bf_throw(BFE_OK);
                        case BFI_IO_INPUT: ctx.mem[ctx.mc] = env->read(env->input); break;
                        case BFI_MEMSET_ZERO: ctx.mem[ctx.mc] = 0; break;
                        case BFI_MOV_RT_UNTIL_ZERO: {
                            bft_cell* last_cell = ctx.mem + BFC_MAX_MEMORY - 1;
                            bft_cell* zero = ctx.mem + ctx.mc;
                            while (zero < last_cell && *zero != 0) ++zero;
                            if (*zero) bf_throw(BFE_MEMORY_CORRUPTION);
                            ctx.mc = zero - ctx.mem;
                        } break;
                        case BFI_MOV_LT_UNTIL_ZERO: {
                            bft_cell* zero = ctx.mem + ctx.mc;
                            while (ctx.mem < zero && *zero != 0) --zero;
                            if (*zero) bf_throw(BFE_MEMORY_CORRUPTION);
                            ctx.mc = zero - ctx.mem;
                        } break;
                        case BFI_BREAKPOINT:
                            if (ext_ctx) *ext_ctx = ctx;
                            bf_throw(BFE_BREAKPOINT);
                        default: bf_throw(BFE_UNKNOWN_INSTR);
                    }
                else if ((instr & BFM_KIND_3BIT) == BFK_EXT_EX)
                    switch (instr & BFM_KIND_8BIT) {
                        case BFI_OUTNTIMES:
                            for (size_t i = 0; i <= (instr & BFM_EX_ARG); i++)
                                env->write(ctx.mem[ctx.mc], env->output);
                            break;
                        case BFI_DMOV_RT: {
                            uint8_t offset = instr & BFM_EX_ARG;
                            ctx.mem[ctx.mc + offset] += ctx.mem[ctx.mc];
                            ctx.mem[ctx.mc] = 0;
                        } break;
                        case BFI_DMOV_LT: {
                            uint8_t offset = instr & BFM_EX_ARG;
                            ctx.mem[ctx.mc - offset] += ctx.mem[ctx.mc];
                            ctx.mem[ctx.mc] = 0;
                        } break;
                        default: bf_throw(BFE_UNKNOWN_INSTR);
                    }
                break;
        }
    }

    return BFE_UNREACHABLE;
cleanup:
    if (rc != BFE_BREAKPOINT) free(ctx.mem);
    return rc;
}

int bfd_print_instr(bft_instr opcode, bft_instr next, FILE* dest) {
    fprintf(dest, "%04hx ", opcode);
    if ((opcode & BFM_KIND_2BIT) == BFK_JMP && (opcode & BFK_JMP_IS_LONG))
        fprintf(dest, "%04hx", next); else fprintf(dest, "    ");
    fputc(' ', dest);
    switch (opcode & BFM_KIND_3BIT) {
        case BFK_INC: fprintf(dest, "increment by %lli",  bf_sign_extend_14(opcode)); break;
        case BFK_DEC: fprintf(dest, "decrement by %lli", -bf_sign_extend_14(opcode)); break;
        case BFK_MOV_RT: fprintf(dest, "move rigth by %lli",  bf_sign_extend_14(opcode)); break;
        case BFK_MOV_LT: fprintf(dest, "move left  by %lli", -bf_sign_extend_14(opcode)); break;
        case BFI_JZ:
            if (opcode & BFK_JMP_IS_LONG) {
                size_t dist = ((opcode & BFM_12BIT) << 16) + next + 1;
                fprintf(dest, "jump ahead by %zu", dist);
                return 2;
            } else
                fprintf(dest, "jump ahead by %u", opcode & BFM_12BIT);
            break;
        case BFI_JNZ:
            if (opcode & BFK_JMP_IS_LONG) {
                size_t dist = ((opcode & BFM_12BIT) << 16) + next + 1;
                fprintf(dest, "jump back %zu", dist);
                return 2;
            } else
                fprintf(dest, "jump back %u", opcode & BFM_12BIT);
            break;
        case BFK_EXT_IM:
            switch (opcode) {
                case BFI_IO_INPUT: fprintf(dest, "input character"); break;
                case BFI_MEMSET_ZERO: fprintf(dest, "set zero value"); break;
                case BFI_MOV_RT_UNTIL_ZERO: fprintf(dest, "move to right until it's zero"); break;
                case BFI_MOV_LT_UNTIL_ZERO: fprintf(dest, "move to left  until it's zero"); break;
                case BFI_BREAKPOINT: fprintf(dest, "breakpoint"); break;
                default: fprintf(dest, "unknown instruction"); break;
            } break;
        case BFK_EXT_EX:
            switch (opcode & BFM_KIND_8BIT) {
                case BFI_OUTNTIMES: {
                    uint8_t count = opcode & BFM_EX_ARG;
                    fprintf(dest, "output character");
                    if (count) fprintf(dest, " %hhu times", count + 1);
                } break;
                case BFI_DMOV_RT: fprintf(dest, "move value to right by %u", opcode & BFM_EX_ARG); break;
                case BFI_DMOV_LT: fprintf(dest, "move value to left  by %u", opcode & BFM_EX_ARG); break;
                default: fprintf(dest, "unknown instruction"); break;
            } break;
    }
    return 1;
}

void bfd_instrs_dump_txt(bft_program* prog, FILE* dest, size_t limit) {
    const int addr_width = prog->count > 2
        ? floor(log10(prog->count - 2)) + 1 : 1;

    bft_instr* instr = prog->items;
    for (size_t i = 0; i < limit && *instr != BFI_DEAD;) {
        fprintf(dest, "[%*zu]: ", addr_width, i);
        int count = bfd_print_instr(instr[0], instr[1], dest);
        i += count; instr += count;
        fputc('\n', dest);
    }

    if (*instr != BFI_DEAD)
        fprintf(dest, "...\n");
}

void bfd_memory_dump_txt(bft_context* ctx, FILE* dest, size_t offset, size_t size) {
    size_t min_size = size < (BFC_MAX_MEMORY - offset) ? size : (BFC_MAX_MEMORY - offset);
    uint8_t buffer[32]; size_t rdlen;
    do {
        rdlen = (min_size >= sizeof buffer) ? sizeof buffer : min_size;
        if (rdlen == 0) break;

        memcpy(buffer, ctx->mem + offset, rdlen);
        offset   += rdlen;
        min_size -= rdlen;

        for (size_t i = 1; i <= rdlen; i++)
            fprintf(dest, "%02hhx%*s", buffer[i-1], i % 8 ? 1 : 2, "");
        fputc('\n', dest);
    } while (rdlen == sizeof buffer);
}

void bfd_memory_dump_bin(bft_context* ctx, FILE* dest, size_t offset, size_t size) {
    size_t min_size = size < BFC_MAX_MEMORY - offset ? size : BFC_MAX_MEMORY - offset;
    fwrite(ctx->mem + offset, 1, min_size, dest);
}

void bfd_memory_dump_loc(bft_context* ctx, FILE* dest) {
    for (int i = -9; i < 10; i++) {
        fprintf(dest, "%*s", (int)(sizeof(bft_cell) - 1), "");
        fprintf(dest, "%+i", i);
        fprintf(dest, "%*s", (int)(sizeof(bft_cell) - 1), "");
        fputc(' ', dest);
    }
    fputc('\n', dest);

    bft_cell *begin = ctx->mem, *end = begin + BFC_MAX_MEMORY, *cell = begin + ctx->mc;
    for (int i = -9; i < 10; i++) {
        bft_cell* cur = cell + i;
        if (begin <= cur && cur < end)
            fprintf(dest, "%0*x ", (int)(sizeof(bft_cell) * 2), *cur);
        else
            fprintf(dest, "%.*s ", (int)(sizeof(bft_cell) * 2), "----------------");
    }
    fputc('\n', dest);
}