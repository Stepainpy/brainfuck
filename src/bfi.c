#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "brainfuck.h"

#ifdef _WIN32
#define PATH_DELIM '\\'
#else
#define PATH_DELIM '/'
#endif

static char* read_file(const char* filename) {
    long size; char* data = NULL;

    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END)) goto cleanup;
    if ((size = ftell(file)) < 0) goto cleanup;
    if (fseek(file, 0, SEEK_SET)) goto cleanup;

    data = malloc(size + 1);
    if (data && fread(data, size, 1, file))
        data[size] = '\0';

cleanup:
    fclose(file);
    return data;
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
        fprintf(stderr, "ERROR: no file provided\n");
        fprintf(stderr, "USAGE: %s <code.bf> [<input.txt>]\n", exename);
        return EXIT_FAILURE;
    }

    const char* path = *argv++; --argc;
    char* code_text = read_file(path);
    if (!code_text) {
        fprintf(stderr, "WARNING: provided empty file\n");
        return EXIT_SUCCESS;
    }

    FILE* input = stdin;
    if (argc >= 1) {
        const char* input_path = *argv++; --argc;
        input = fopen(input_path, "r");
        if (!input) {
            fprintf(stderr, "ERROR: cannot open input file\n");
            free(code_text);
            return EXIT_FAILURE;
        }
    }

    bft_error rc = BFE_OK;
    bft_program* program = NULL;
    bft_env env = {
        input, stdout,
        bf_read, bf_write
    };

    rc = bfa_compile(&program, code_text, strlen(code_text));
    if (rc) goto cleanup;
    rc = bfa_execute(program, &env);

cleanup:
    if (input && input != stdin) fclose(input);
    if (rc) fprintf(stderr, "\rERROR: %s\n", bfa_strerror(rc));
    bfa_destroy(program);
    free(code_text);
    return rc;
}