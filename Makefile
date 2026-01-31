# Minimal portable Makefile (Buildroot-friendly)

TARGET  ?= rpi4kerneldriver
SRC     ?= keypadDriver.c
OBJ     := $(SRC:.c=.o)
obj-m += keypadDriver.o

# Toolchain (Buildroot will override CC automatically)
CC      ?= gcc
CFLAGS  ?= -O2 -Wall
LDFLAGS ?=

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean