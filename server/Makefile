# Makefile for aesdsocket

# Compiler
CC ?= gcc
CROSS_COMPILE ?=

# Compiler flags
CFLAGS = -Wall -Werror -g

# Target executable
TARGET = aesdsocket

# Source files
SRCS = aesdsocket.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile source files
%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(TARGET) $(OBJS)

# Default target (same as all)
default: all

.PHONY: all clean default
