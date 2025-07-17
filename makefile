.PHONY: all clean

CC = cc
CFLAGS += -O2 -std=c99 -s -Iinc
CFLAGS += -Wall -Wextra -pedantic
LFLAGS += -L. -lbf
ifdef OS
TARGET = bfi.exe
else
TARGET = bfi
endif

all: $(TARGET) libbf.a

# create bfi
$(TARGET): bin/bfi.o libbf.a
	$(CC) -o $@ $^ $(LFLAGS)

bin/bfi.o: bfi.c inc/brainfuck.h | bin
	$(CC) -c -o $@ $< $(CFLAGS)

# create library
libbf.a: bin/brainfuck.o
	ar rsc $@ $^

bin/%.o: src/%.c inc/brainfuck.h | bin
	$(CC) -c -o $@ $< $(CFLAGS)

# other stuff
bin:
	mkdir bin

clean:
	rm -f *.a *.exe bin/*.o