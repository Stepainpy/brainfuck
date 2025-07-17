#ifndef BRAINFUCK_CONF_H
#define BRAINFUCK_CONF_H

#include <stdint.h>

#define BFD_MEMORY_CAPACITY 32768
#define BFD_BREAKPOINT_CHAR '@'

typedef uint8_t bft_cell;
typedef uint16_t bft_instr;

typedef void (*bft_ifunc)(void*, bft_cell*);
typedef void (*bft_ofunc)(void*, bft_cell );

typedef struct bft_program {
    bft_instr* items;
    size_t     count;
} bft_program;

typedef struct bft_env {
    void *input, *output;
    bft_ifunc  read;
    bft_ofunc write;
} bft_env;

typedef struct bft_context {
    size_t pc, mc;
    bft_cell* mem;
} bft_context;

typedef enum bft_error {
    BFE_OK = 0,
    BFE_BREAKPOINT,
    BFE_UNREACHABLE,
    BFE_NULL_POINTER,
    BFE_NO_MEMORY,
    BFE_STACK_OVERFLOW,
    BFE_UNBALANCED_BRACKETS,
    BFE_VERY_LONG_JUMP,
    BFE_INVALID_ENV,
    BFE_UNKNOWN_INSTR,
    BFE_MEMORY_CORRUPTION,
} bft_error;

#endif // BRAINFUCK_CONF_H