#include "brainfuck.h"
#include <stdlib.h>

const char* bfa_strerror(bft_error error) {
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch-enum"
    switch (error) {
        default: return "unknown error";
        case BFE_OK: return "no errors";
        case BFE_BREAKPOINT: return "breakpoint in code";
        case BFE_UNREACHABLE: return "return from unreachable point";
        case BFE_NULL_POINTER: return "null pointer passed";
        case BFE_NO_MEMORY: return "there is no memory to allocate";
        case BFE_STACK_OVERFLOW: return "the maximum stack depth for brackets has been reached";
        case BFE_UNBALANCED_BRACKETS: return "unbalanced brackets";
        case BFE_VERY_LONG_JUMP: return "the relative jump is too long";
        case BFE_INVALID_ENV: return "invalid values in environment";
        case BFE_UNKNOWN_INSTR: return "unknown instruction";
        case BFE_MEMORY_CORRUPTION: return "memory corruption";
    }
#pragma GCC diagnostic pop
}

void bfa_destroy(bft_program* prog) {
    if (prog) free(prog->items);
}