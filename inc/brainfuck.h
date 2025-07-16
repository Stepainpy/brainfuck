/* Prefix cheat sheet
 * bf* -> brainfuck *
 *   t -> type
 *   a -> API
 *   d -> debug
 *   E -> error
 */

/* Using breakpoints in code:
 * example: ++++>>>@--<<<
 * When current char is '@' break execute
 * program and save context by passed pointer.
 * For rerun program need pass saved context.
 */

#ifndef BRAINFUCK_H
#define BRAINFUCK_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t bft_cell;
typedef uint16_t bft_instr;
typedef uint8_t (*bft_ifunc)(         void*);
typedef void    (*bft_ofunc)(uint8_t, void*);

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

const char* bfa_strerror(bft_error error);

bft_error bfa_compile(bft_program* program, const char* code, size_t size);
bft_error bfa_execute(bft_program* program, bft_env* env, bft_context* ctx);
void      bfa_destroy(bft_program* program);

int bfd_print_instr(bft_instr opcode, bft_instr next, FILE* dest);
void bfd_instrs_dump_txt(bft_program* program, FILE* dest, size_t limit);
void bfd_memory_dump_txt(bft_context* context, FILE* dest, size_t offset, size_t size);
void bfd_memory_dump_bin(bft_context* context, FILE* dest, size_t offset, size_t size);
void bfd_memory_dump_loc(bft_context* context, FILE* dest);

#endif // BRAINFUCK_H