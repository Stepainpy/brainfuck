#include "brainfuck.h"
#include "bfcommon.h"
#include <string.h>
#include <math.h>

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
                case BFI_MUL_RT: fprintf(dest, "add to right cell value mul by %u", opcode & BFM_EX_ARG); break;
                case BFI_MUL_LT: fprintf(dest, "add to left  cell value mul by %u", opcode & BFM_EX_ARG); break;
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