#include "brainfuck.h"
#include "bfcommon.h"
#include <stdbool.h>
#include <stdlib.h>

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