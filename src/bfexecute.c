#include "brainfuck.h"
#include "bfcommon.h"
#include <stdbool.h>
#include <stdlib.h>

static inline bft_error cyclic_movadd(bft_context* ctx, bft_cell coef, size_t offset) {
    if (ctx->mem[ctx->mc] == 0) return BFE_OK;
    if (ctx->mc + offset >= BFC_MAX_MEMORY)
        return BFE_MEMORY_CORRUPTION;

    ctx->mem[ctx->mc + offset] += ctx->mem[ctx->mc] * coef;
    ctx->mem[ctx->mc] = 0;
    return BFE_OK;
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
        switch (instr & BFM_KIND_3BIT) {
            case BFK_INC: case BFK_DEC:
                ctx.mem[ctx.mc] += bfu_sign_extend_14(instr);
                break;
            case BFK_MOV_RT: case BFK_MOV_LT:
                ctx.mc += bfu_sign_extend_14(instr);
                if (ctx.mc >= BFC_MAX_MEMORY)
                    bfu_throw(BFE_MEMORY_CORRUPTION);
                break;
            case BFI_JEZ: case BFI_JNZ: {
                bool   zbit = instr & BFM_JMP_ZBIT;
                size_t dist = instr & BFM_12BIT;
                if (instr & BFK_JMP_IS_LONG)
                    dist = (dist << 16) + prog->items[ctx.pc++] + 1;
                if ((bool)ctx.mem[ctx.mc] == zbit)
                    ctx.pc += zbit ? -dist : dist;
            } break;
            case BFK_EXT_IM:
                switch (instr) {
                    case BFI_DEAD: bfu_throw(BFE_OK);
                    case BFI_IO_INPUT: env->read(env->input, ctx.mem + ctx.mc); break;
                    case BFI_MEMSET_ZERO: ctx.mem[ctx.mc] = 0; break;
                    case BFI_MOV_RT_UNTIL_ZERO: {
                        bft_cell* last_cell = ctx.mem + BFC_MAX_MEMORY - 1;
                        bft_cell* zero = ctx.mem + ctx.mc;
                        while (zero < last_cell && *zero != 0) ++zero;
                        if (*zero) bfu_throw(BFE_MEMORY_CORRUPTION);
                        ctx.mc = zero - ctx.mem;
                    } break;
                    case BFI_MOV_LT_UNTIL_ZERO: {
                        bft_cell* zero = ctx.mem + ctx.mc;
                        while (ctx.mem < zero && *zero != 0) --zero;
                        if (*zero) bfu_throw(BFE_MEMORY_CORRUPTION);
                        ctx.mc = zero - ctx.mem;
                    } break;
                    case BFI_BREAKPOINT:
                        if (ext_ctx) *ext_ctx = ctx;
                        bfu_throw(BFE_BREAKPOINT);
                    default: bfu_throw(BFE_UNKNOWN_INSTR);
                } break;
            case BFK_EXT_EX:
                switch (instr & BFM_KIND_8BIT) {
                    case BFI_OUTNTIMES:
                        for (size_t i = 0; i <= (instr & BFM_EX_ARG); i++)
                            env->write(env->output, ctx.mem[ctx.mc]);
                        break;
                    case BFI_CYCLIC_ADD_RT:
                    case BFI_CYCLIC_ADD_LT:
                        if (cyclic_movadd(&ctx, instr & BFM_EX_ARG,
                            (instr & BFM_KIND_8BIT) == BFI_CYCLIC_ADD_RT ? 1 : -1))
                            bfu_throw(BFE_MEMORY_CORRUPTION);
                        break;
                    case BFI_CYCLIC_MOV_RT:
                    case BFI_CYCLIC_MOV_LT:
                        if (cyclic_movadd(&ctx, 1,
                            (instr & BFM_KIND_8BIT) == BFI_CYCLIC_MOV_RT
                            ? (instr & BFM_EX_ARG) : -(instr & BFM_EX_ARG)))
                            bfu_throw(BFE_MEMORY_CORRUPTION);
                        break;
                    case BFI_CYCLIC_MOVADD_RT:
                    case BFI_CYCLIC_MOVADD_LT: {
                        if (cyclic_movadd(&ctx, instr & 0xF,
                            (instr & BFM_KIND_8BIT) == BFI_CYCLIC_MOVADD_RT
                            ? (instr >> 4 & 0xF) : -(instr >> 4 & 0xF)))
                            bfu_throw(BFE_MEMORY_CORRUPTION);
                    } break;
                    default: bfu_throw(BFE_UNKNOWN_INSTR);
                } break;
        }
    }

    return BFE_UNREACHABLE;
cleanup:
    if (rc != BFE_BREAKPOINT) free(ctx.mem);
    return rc;
}