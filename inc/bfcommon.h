#ifndef BRAINFUCK_COMMON_H
#define BRAINFUCK_COMMON_H

#include "bfconf.h"

#define BFD_NBIT_MAX(bitcount) ((1 << (bitcount)) - 1)

static struct bft_int14 { int16_t x : 14; } s14bit;
#define BFD_INT14_MIN (-8192)
#define BFD_INT14_MAX   8191
#define bfu_sign_extend_14(integer) \
    (int64_t)(s14bit.x = (integer) & BFM_14BIT)

#define bfu_throw(rc_) do { rc = rc_; goto cleanup; } while (0)
#define bfu_abs(x) ((x) < 0 ? -(x) : (x))

enum {
    BFM_16BIT = BFD_NBIT_MAX(16),
    BFM_14BIT = BFD_NBIT_MAX(14),
    BFM_12BIT = BFD_NBIT_MAX(12),
    BFM_JMP_ZBIT = 0x2000,
    BFM_EX_ARG = 0x3FF,
};

enum {
    BFM_KIND_2BIT = 0xC000,
    BFM_KIND_3BIT = 0xE000,
    BFM_KIND_5BIT = 0xF800,
};

enum {
    BFC_MAX_MEMORY = BFD_MEMORY_CAPACITY,
    BFC_MAX_MEMORY_BYTES = BFC_MAX_MEMORY * sizeof(bft_cell),
    BFC_MAX_JUMP_SH_DIST = BFD_NBIT_MAX(12),
    BFC_MAX_JUMP_LO_DIST = BFD_NBIT_MAX(28),
    BFC_EX_ARG_MAX = BFD_NBIT_MAX(10),
};

/* Structure of virtual machine instructions
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
 *     |1|E-I|D|        arg        | - instruction within value
 *    -+-+-+-+-+-+-+-+-+-+-+-+-+-+-'
 *
 * Note: halt instruction has value 0xDEAD
 */

enum {
    BFI_CHG = 0 << 14,
        BFK_INC = BFI_CHG | 0 << 13,
        BFK_DEC = BFI_CHG | 1 << 13,
    BFI_MOV = 1 << 14,
        BFK_MOV_RT = BFI_MOV | 0 << 13,
        BFK_MOV_LT = BFI_MOV | 1 << 13,
    BFK_JMP = 2 << 14,
        BFK_JMP_IS_LONG  =  1 << 12,
        BFI_JEZ = BFK_JMP | 0 << 13,
        BFI_JNZ = BFK_JMP | 1 << 13,
    BFK_EXT = 3 << 14,
        BFK_EXT_IM = BFK_EXT | 0 << 13,
            BFI_DEAD = 0xDEAD,
            BFI_IO_INPUT = BFK_EXT_IM,
            BFI_MOV_RT_UNTIL_ZERO,
            BFI_MOV_LT_UNTIL_ZERO,
            BFI_MEMSET_ZERO,
            BFI_BREAKPOINT,
        BFK_EXT_EX = BFK_EXT | 1 << 13,
            BFK_EXT_EX_IS_LEFT = 1 << 10,
            BFI_OUTNTIMES     = BFK_EXT_EX | 0 << 11,
            BFI_CYCLIC_ADD    = BFK_EXT_EX | 1 << 11,
            BFI_CYCLIC_MOV    = BFK_EXT_EX | 2 << 11,
            BFI_CYCLIC_MOVADD = BFK_EXT_EX | 3 << 11,
};

#endif // BRAINFUCK_COMMON_H