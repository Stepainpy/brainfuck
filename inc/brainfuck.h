/* Prefix cheat sheet
 * bf* -> brainfuck *
 *   t -> type
 *   a -> API
 *   d -> debug
 *   E -> error
 */

#ifndef BRAINFUCK_H
#define BRAINFUCK_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef struct bft_program bft_program;

typedef uint8_t bft_cell;
typedef uint8_t (*bft_ifunc)(         void*);
typedef void    (*bft_ofunc)(uint8_t, void*);

typedef struct bft_env {
    void *input, *output;
    bft_ifunc  read;
    bft_ofunc write;
} bft_env;

typedef enum bft_error {
    BFE_OK = 0,
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

bft_error bfa_compile(bft_program** program, const char* code, size_t size);
bft_error bfa_execute(bft_program*  program, bft_env* env);
void      bfa_destroy(bft_program*  program);

void bfd_instrs_dump_txt(bft_program* program, FILE* dest, size_t limit);
void bfd_memory_dump_txt(bft_program* program, FILE* dest, size_t offset, size_t size);
void bfd_memory_dump_bin(bft_program* program, FILE* dest, size_t offset, size_t size);

#endif // BRAINFUCK_H