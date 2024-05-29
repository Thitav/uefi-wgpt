.POSIX:
.PHONY: all clean

CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -O2
TARGET = wpgt

all: $(TARGET)

clean:
	rm -f $(TARGET)
