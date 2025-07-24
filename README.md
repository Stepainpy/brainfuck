# Brainfuck virtual machine

## Overview

Little Brainfuck virtual machine. Brainfuck source code convert to optimized byte-code for machine.
Support interruption via breakpoints (see [bf source](./bf.c)).

## Usage

### As standalone code

``` console
$ bf <code.bf> [-A] [<inputfile>]
```

### As external part

1. compile library.
2. copy `bfconf.h`, `brainfuck.h` and library file to your project.
3. use API functions `bfa_compile`, `bfa_execute` and etc.

Minimal code example:
```c
#include <stdio.h>
#include <string.h>
#include "brainfuck.h"

const char* source = // print "brainfuck"
">++++[>++++++<-]>-[[<+++++>>+<-]>-]<<[<]>>>>-"
"-.<<<-.>>>-.<.<.>---.<<+++.>>>++.<<---.[>]<<.";

static void read(void* file, bft_cell* cell) {
    int ch = fgetc(file);
    *cell = ch != EOF ? ch : 0;
}

static void write(void* file, bft_cell cell) {
    fputc(cell, file);
}

int main(void) {
    bft_error rc = BFE_OK;
    bft_program program = {0};
    bft_env env = { stdin, stdout, read, write };

    rc = bfa_compile(&program, source, strlen(source));
    if (rc) { // error handling
        fprintf(stderr, "bfa_compile(): %s\n", bfa_strerror(rc));
        return 1;
    }
    rc = bfa_execute(&program, &env, NULL); // no breakpoints
    if (rc) { // error handling
        bfa_destroy(&program);
        fprintf(stderr, "bfa_execute(): %s\n", bfa_strerror(rc));
        return 1;
    }
    bfa_destroy(&program);

    return 0;
}
```

## Prefix cheatsheet

| Prefix | Full name   |
| :----: | :---------- |
|  `t`   | type        |
|  `a`   | API         |
|  `d`   | debug       |
|  `s`   | stack       |
|  `c`   | code        |
|  `p`   | parse       |
|  `u`   | utility     |
|  `i`   | instruction |
|  `I`   | instruction |
|  `E`   | error       |
|  `M`   | mask        |
|  `C`   | constant    |
|  `K`   | kind        |
|  `D`   | define      |