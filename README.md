# Brainfuck interpreter

## Overview

Interpreter has several optimization in code generation. Have API for embedding usage.

## Usage

### As standalone code

``` console
$ bfi <code.bf> [<inputfile>]
```

### As external part

1. compile library file. (run `make` command)
2. copy `bfconf.h`, `brainfuck.h` and `libbf.a` to your project.
3. use API functions `bfa_compile` and `bfa_execute`.

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