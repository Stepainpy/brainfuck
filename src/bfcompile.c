#include "brainfuck.h"
#include "bfcommon.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define INVAL_INDEX ((size_t)-1)
#define PAREN_STACK_DEPTH 1023

static struct bft_paren_stack {
    size_t head, positions[PAREN_STACK_DEPTH];
} paren_stack;

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

typedef struct {
    bft_instr* items;
    size_t count, capacity;
} bft_instrs;

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

static void bfc_erase(bft_instrs* code, size_t pos, size_t len) {
    memmove(code->items + pos, code->items + pos + len,
        (code->count - pos - len) * sizeof *code->items);
    code->count -= len;
}

#define bfi_last(src) src->items[src->count - 1]

#define bfi_push(dest, instr) do { \
    if (bfc_push(dest, instr)) \
        bfu_throw(BFE_NO_MEMORY); \
} while (0)

#define bfi_insert(dest, instr, pos) do { \
    if (bfc_insert(dest, instr, pos)) \
        bfu_throw(BFE_NO_MEMORY); \
} while (0)

static bool bfi_prev_is(bft_instrs* code, int type) {
    return code->count && (bfi_last(code) & BFM_KIND_2BIT) == type;
}

static bool bfp_is_oper(char ch) {
    return ch == ',' || ch == '.' || ch == '+' || ch == '-'
        || ch == '>' || ch == '<' || ch == '[' || ch == ']'
        || ch == BFD_BREAKPOINT_CHAR;
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
            if (acc->x < BFD_INT14_MAX) ++acc->x; else return ptr;
        } else if (*ptr == dec) {
            if (acc->x > BFD_INT14_MIN) --acc->x; else return ptr;
        } else return ptr;
        ptr = bfp_next_oper(ptr + 1, end);
    }
    return ptr;
}

static bft_error bfp_collapse_instr(bft_instrs* code, int type, struct int14_t cur_acc) {
    bft_error rc = BFE_OK;
    /**/ if (cur_acc.x == 0) return rc;
    else if (bfi_prev_is(code, type)) {
        int32_t prev_acc = bfu_sign_extend_14(bfi_last(code));
        /*  */ if ((prev_acc < 0 && cur_acc.x > 0) || (prev_acc > 0 && cur_acc.x < 0)) {
            struct int14_t new_acc = { cur_acc.x + prev_acc };
            if (new_acc.x == 0) { --code->count; return rc; }
            bfi_last(code) = type | (new_acc.x & BFM_14BIT);
        } else if ((prev_acc < 0 && cur_acc.x < 0) || (prev_acc > 0 && cur_acc.x > 0)) {
            int32_t new_acc = prev_acc + cur_acc.x;
            /*  */ if (new_acc < BFD_INT14_MIN) {
                bfi_last(code) = type | (BFD_INT14_MIN & BFM_14BIT);
                bfi_push(code, type | ((new_acc - BFD_INT14_MIN) & BFM_14BIT));
            } else if (new_acc > BFD_INT14_MAX) {
                bfi_last(code) = type | (BFD_INT14_MAX & BFM_14BIT);
                bfi_push(code, type | ((new_acc - BFD_INT14_MAX) & BFM_14BIT));
            } else
                bfi_last(code) = type | (new_acc & BFM_14BIT);
        } else
            return BFE_UNREACHABLE;
    } else
        bfi_push(code, type | (cur_acc.x & BFM_14BIT));
cleanup:
    return rc;
}

/*
 * find patterns:
 * [-(>|<)*n+*n(<|>)*n]
 * [(>|<)*n+*n(<|>)*n-]
 */
static bool bfp_find_cycled_ops(bft_instrs* code, size_t jz_pos) {
    bft_instr i1, i2, i3, i4;
    i1 = code->items[jz_pos + 1];
    i2 = code->items[jz_pos + 2];
    i3 = code->items[jz_pos + 3];
    i4 = code->items[jz_pos + 4];
    int movn, addn;

    /*  */ if ((i1 & BFM_KIND_3BIT) == BFK_DEC) { // minus 1
        if (bfu_sign_extend_14(i1) != -1)         return false;
        if ((i2 & BFM_KIND_2BIT) != BFI_MOV)      return false; // mov by x
        movn = bfu_sign_extend_14(i2);
        if ((i3 & BFM_KIND_3BIT) != BFK_INC)      return false; // plus n
        addn = bfu_sign_extend_14(i3);
        if ((i4 & BFM_KIND_2BIT) != BFI_MOV)      return false; // mov by -x
        if (bfu_sign_extend_14(i4) != -movn)      return false;
        /* here is all good */
    } else if ((i1 & BFM_KIND_2BIT) == BFI_MOV) { // mov by x
        movn = bfu_sign_extend_14(i1);
        if ((i2 & BFM_KIND_3BIT) != BFK_INC)      return false; // plus n
        addn = bfu_sign_extend_14(i2);
        if ((i3 & BFM_KIND_2BIT) != BFI_MOV)      return false; // mov by -x
        if (bfu_sign_extend_14(i3) != -movn)      return false;
        if ((i4 & BFM_KIND_3BIT) != BFK_DEC)      return false; // minus 1
        if (bfu_sign_extend_14(i4) != -1)         return false;
        /* here is all good */
    } else // no pattern starts
        return false;

    /**/ if (bfu_abs(movn) == 1 && addn <= BFC_EX_ARG_MAX)
        code->items[jz_pos] = BFI_CYCLIC_ADD | (addn & BFM_EX_ARG);
    else if (addn == 1 && bfu_abs(movn) <= BFC_EX_ARG_MAX)
        code->items[jz_pos] = BFI_CYCLIC_MOV | (bfu_abs(movn) & BFM_EX_ARG);
    else if (addn < 32 && bfu_abs(movn) < 32)
        code->items[jz_pos] = BFI_CYCLIC_MOVADD | (bfu_abs(movn) & 0x1F) << 5 | (addn & 0x1F);
    else
        return false;

    if (movn < 0) code->items[jz_pos] |= BFK_EXT_EX_IS_LEFT;
    code->count = jz_pos + 1;
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
            case BFD_BREAKPOINT_CHAR: bfi_push(code, BFI_BREAKPOINT); break;
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
                        bfu_throw(BFE_STACK_OVERFLOW);
                    bfi_push(code, BFI_JEZ); // placeholder
                }
            } break;
            case ']': {
                size_t pos = bfs_pop(&paren_stack);
                if (pos == INVAL_INDEX)
                    bfu_throw(BFE_UNBALANCED_BRACKETS);

                size_t dist = code->count - pos;
                /*  */ if (dist > BFC_MAX_JUMP_LO_DIST) {
                    bfu_throw(BFE_VERY_LONG_JUMP);
                } else if (dist > BFC_MAX_JUMP_SH_DIST) {
                    code->items[pos] = BFI_JEZ | BFK_JMP_IS_LONG | (dist >> 16);
                    bfi_insert(code,   dist & BFM_16BIT, pos + 1);
                    bfi_push(code,     BFI_JNZ | BFK_JMP_IS_LONG | (dist >> 16));
                    bfi_push(code,     dist & BFM_16BIT);
                } else {
                    /**/ if (dist == 5 && bfp_find_cycled_ops(code, pos)) break;
                    else {
                        code->items[pos] = BFI_JEZ | dist;
                        bfi_push(code,     BFI_JNZ | dist);
                    }
                }
            } break;
        }
    }

    if (paren_stack.head > 0)
        bfu_throw(BFE_UNBALANCED_BRACKETS);
    bfi_push(code, BFI_DEAD);

    while ((code->items[0] & BFM_KIND_3BIT) == BFI_JEZ) {
        bft_instr curr = code->items[0];
        bft_instr next = code->items[1];

        size_t size = curr & BFM_12BIT;
        if (curr & BFK_JMP_IS_LONG)
            size = (size << 16) + next;
        size += curr & BFK_JMP_IS_LONG ? 3 : 1;

        bfc_erase(code, 0, size);
    }

    prog->count = code->count;
    prog->items = realloc(code->items,
        code->count * sizeof *code->items);

    return prog->items ? BFE_OK : BFE_NO_MEMORY;
cleanup:
    free(code->items);
    return rc;
}