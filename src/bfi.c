#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "brainfuck.h"

#ifdef _WIN32
#define PATH_DELIM '\\'
#else
#define PATH_DELIM '/'
#endif

#define  INFO_PREFIX "[\x1b[34mINFO\x1b[0m]: "
#define ERROR_PREFIX "[\x1b[31mERROR\x1b[0m]: "
#define USAGE_PREFIX "[\x1b[32mUSAGE\x1b[0m]: "
#define  WARN_PREFIX "[\x1b[33mWARNING\x1b[0m]: "

static char* read_file(const char* filename) {
    long size; char* data = NULL;

    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END)) goto cleanup;
    if ((size = ftell(file)) < 0) goto cleanup;
    if (fseek(file, 0, SEEK_SET)) goto cleanup;

    data = malloc(size + 1);
    if (data) {
        fread(data, size, 1, file);
        data[size] = '\0';
    }

cleanup:
    fclose(file);
    return data;
}

static void usage(const char* exename) {
    fprintf(stderr, USAGE_PREFIX "\n  %s <code.bf> [OPTIONS] [<input.txt>]\n", exename);
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  -A   Write to <code.bfa> instructions for machine\n");
}

static uint8_t bf_read(void* file) {
    int ch = fgetc(file);
    return ch != EOF ? ch : 0;
}

static void bf_write(uint8_t byte, void* file) {
    fputc(byte, file);
}

int main(int argc, char** argv) {
    const char* exename = *argv++; --argc;
    const char* last_delim = strrchr(exename, PATH_DELIM);
    if (last_delim) exename = last_delim + 1;

    if (argc < 1) {
        fprintf(stderr, ERROR_PREFIX "no file provided\n");
        usage(exename);
        return EXIT_FAILURE;
    }

    const char* path = *argv++; --argc;
    char* code_text = read_file(path);
    if (!code_text) {
        fprintf(stderr, ERROR_PREFIX "cannot load file content\n");
        return EXIT_FAILURE;
    } else if (code_text[0] == '\0') {
        fprintf(stderr, WARN_PREFIX "provided empty file\n");
        free(code_text);
        return EXIT_SUCCESS;
    }

    bool output_asm = false;

    if (argc >= 1) {
        if (strcmp(*argv, "-A") == 0) {
            output_asm = true;
            ++argv; --argc;
        }
    }

    FILE* input = stdin;
    if (argc >= 1) {
        const char* input_path = *argv++; --argc;
        input = fopen(input_path, "r");
        if (!input) {
            fprintf(stderr, ERROR_PREFIX "cannot open input file\n");
            free(code_text);
            return EXIT_FAILURE;
        }
    }

    bft_error rc = BFE_OK;
    bft_program program = {0};
    bft_env env = {
        input, stdout,
        bf_read, bf_write
    };

    rc = bfa_compile(&program, code_text, strlen(code_text));
    if (rc) goto cleanup;

    if (output_asm) {
        static char bfasm_path[272] = {0};
        strcpy(bfasm_path, path);
        strcat(bfasm_path, ".bfa");

        FILE* asmf = fopen(bfasm_path, "w");
        if (asmf) {
            bfd_instrs_dump_txt(&program, asmf, -1);
            fclose(asmf);
        } else
            fprintf(stderr, ERROR_PREFIX "cannot open assembler file\n");
    }

    bft_context context = {0};
    do {
        rc = bfa_execute(&program, &env, &context);
        if (rc == BFE_BREAKPOINT) {
            fprintf(stderr, "\n" INFO_PREFIX "dump local memory:\n");
            bfd_memory_dump_loc(&context, stderr);
        }
    } while (rc == BFE_BREAKPOINT);

cleanup:
    if (input && input != stdin) fclose(input);
    if (rc) fprintf(stderr, "\n" ERROR_PREFIX "%s\n", bfa_strerror(rc));
    bfa_destroy(&program);
    free(code_text);
    return rc;
}