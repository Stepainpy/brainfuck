#include "brainfuck.h"
#include "bfcommon.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

void bfd_instr_description(bft_instr opcode, bft_instr next, FILE* dest) {
    switch (opcode & BFM_KIND_3BIT) {
        case BFK_INC: fprintf(dest, "increment by %lli",  bfu_sign_extend_14(opcode)); break;
        case BFK_DEC: fprintf(dest, "decrement by %lli", -bfu_sign_extend_14(opcode)); break;
        case BFK_MOV_RT: fprintf(dest, "move rigth by %lli",  bfu_sign_extend_14(opcode)); break;
        case BFK_MOV_LT: fprintf(dest, "move left  by %lli", -bfu_sign_extend_14(opcode)); break;
        case BFI_JEZ:
            if (opcode & BFK_JMP_IS_LONG) {
                size_t dist = ((opcode & BFM_12BIT) << 16) + next + 1;
                fprintf(dest, "jump ahead by %zu", dist);
            } else
                fprintf(dest, "jump ahead by %u", opcode & BFM_12BIT);
            break;
        case BFI_JNZ:
            if (opcode & BFK_JMP_IS_LONG) {
                size_t dist = ((opcode & BFM_12BIT) << 16) + next + 1;
                fprintf(dest, "jump back %zu", dist);
            } else
                fprintf(dest, "jump back %u", opcode & BFM_12BIT);
            break;
        case BFK_EXT_IM:
            switch (opcode) {
                case BFI_IO_INPUT: fprintf(dest, "input character"); break;
                case BFI_BREAKPOINT: fprintf(dest, "breakpoint"); break;
                case BFI_MEMSET_ZERO: fprintf(dest, "set zero value"); break;
                case BFI_MOV_RT_UNTIL_ZERO: fprintf(dest, "move to right until it's zero"); break;
                case BFI_MOV_LT_UNTIL_ZERO: fprintf(dest, "move to left  until it's zero"); break;
                default: fprintf(dest, "unknown instruction"); break;
            } break;
        case BFK_EXT_EX:
            switch (opcode & BFM_KIND_8BIT) {
                case BFI_OUTNTIMES: {
                    uint8_t count = opcode & BFM_EX_ARG;
                    fprintf(dest, "output character");
                    if (count) fprintf(dest, " %hhu times", count + 1);
                } break;
                case BFI_CYCLIC_ADD_RT:
                    fprintf(dest, "add to right by %u cell value mul by %u",
                        opcode >> 4 & 0xF, opcode & 0xF);
                    break;
                case BFI_CYCLIC_ADD_LT:
                    fprintf(dest, "add to left  by %u cell value mul by %u",
                        opcode >> 4 & 0xF, opcode & 0xF);
                    break;
                default: fprintf(dest, "unknown instruction"); break;
            } break;
    }
}

static bool bfu_has_long_loop(bft_instr* instr) {
    while (*instr != BFI_DEAD) {
        if ((*instr & BFM_KIND_2BIT) == BFK_JMP && (*instr & BFK_JMP_IS_LONG))
            return true;
        ++instr;
    }
    return false;
}

void bfd_instrs_dump_txt(bft_program* prog, FILE* dest, size_t limit) {
    const int addr_width = prog->count > 2
        ? floor(log10(prog->count - 2)) + 1 : 1;
    const bool has_long_jmp =
        prog->count > BFC_MAX_JUMP_SH_DIST && bfu_has_long_loop(prog->items);

    int tab = 0;
    bft_instr* instr = prog->items;
    for (size_t i = 0; i < limit && *instr != BFI_DEAD;) {
        fprintf(dest, "[%*zu]: %04hx ", addr_width, i, *instr);
        if (has_long_jmp) {
            if ((*instr & BFM_KIND_2BIT) == BFK_JMP && (*instr & BFK_JMP_IS_LONG))
                fprintf(dest, "%04hx ", instr[1]);
            else
                fprintf(dest, "     ");
        }
        fputs("- ", dest);

        if ((*instr & BFM_KIND_3BIT) == BFI_JNZ) --tab;
        fprintf(dest, "%*s", tab * 2, "");
        if ((*instr & BFM_KIND_3BIT) == BFI_JEZ) ++tab;

        bfd_instr_description(instr[0], instr[1], dest);
        fputc('\n', dest);

        ++i; ++instr;
        if (has_long_jmp && (*instr & BFM_KIND_2BIT) == BFK_JMP && (*instr & BFK_JMP_IS_LONG))
            ++i, ++instr;
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