# Define the cross-compiler prefix
ifdef CROSS_COMPILE
    CC = $(CROSS_COMPILE)gcc
else
    CC = gcc
endif

# Define the target executable
TARGET = writer

# Define source and object files
SRC = writer.c
OBJ = $(SRC:.c=.o)

# Compiler flags
CFLAGS = -Wall -g

# Default target
all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $<

# Rule to compile .c files to .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(OBJ) $(TARGET)

# Force rebuild when CROSS_COMPILE changes
.PHONY: all clean