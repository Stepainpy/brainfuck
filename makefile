.PHONY: all clean

CC = cc
CFLAGS += -O2 -std=c99 -s -Iinc
CFLAGS += -Wall -Wextra -pedantic
LFLAGS += -L. -lbf
ifdef OS
TARGET = $(addsuffix .exe, bfi)
else
TARGET = bfi
endif

all: $(TARGET)

$(TARGET): bin/bfi.o libbf.a
	$(CC) -o $@ $^ $(LFLAGS)

libbf.a: bin/brainfuck.o
	ar rsc $@ $^

bin/%.o: src/%.c inc/brainfuck.h | bin
	$(CC) -c -o $@ $< $(CFLAGS)

bin:
	mkdir bin

clean:
	rm -f *.a *.exe bin/*.o