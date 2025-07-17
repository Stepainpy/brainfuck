#ifndef BRAINFUCK_H
#define BRAINFUCK_H

#include <stdio.h>
#include "bfconf.h"

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

/* Using breakpoints in code:
 * example: ++++>>>@--<<<
 * When current char is '@' break execute
 * program and save context by passed pointer.
 * For rerun program need pass saved context.
 */