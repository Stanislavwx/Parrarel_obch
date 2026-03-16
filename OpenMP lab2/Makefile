CC := gcc
CFLAGS := -O2 -Wall -Wextra -Wpedantic -std=c11 -fopenmp
TARGET := matrix_sum_openmp
SRC := matrix_sum_openmp.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
