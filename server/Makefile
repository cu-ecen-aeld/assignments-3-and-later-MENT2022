# You may override the compiler by setting the CC environment variable,
# which is useful for cross-compilation.
# For example: make CC=arm-linux-gnueabihf-gcc

CC ?= gcc
CFLAGS := -Wall -Wextra -std=gnu11
LDFLAGS := -pthread
TARGET := aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.o
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.o $(LDFLAGS)

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c -o aesdsocket.o

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean default

default: all
