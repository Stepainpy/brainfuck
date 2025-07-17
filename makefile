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

SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,bin/%.o,$(SOURCES))

all: $(TARGET) libbf.a

# create bfi
$(TARGET): bfi.c libbf.a
	$(CC) -o $@ $^ $(CFLAGS) $(LFLAGS)

# create library
libbf.a: $(OBJECTS)
	ar rsc $@ $^

bin/%.o: src/%.c $(wildcard inc/*.h) | bin
	$(CC) -c -o $@ $< $(CFLAGS)

# other stuff
bin:
	mkdir bin

clean:
	rm -f *.a *.exe bin/*.o