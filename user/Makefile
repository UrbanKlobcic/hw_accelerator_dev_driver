CC = gcc
CFLAGS = -I../include -lpthread

executables = bin/dma_mtx_mul_test_v3 
#bin/test_mtx_mul bin/poll_mtx_mul bin/buf_mtx_mul bin/test_open 

.PHONY: all clean

all: $(executables)

bin/test_open: build/test_open.o | bin
	$(CC) $^ -o $@

bin/test_mtx_mul: build/test_mtx_mul.o | bin
	$(CC) $^ -o $@

bin/poll_mtx_mul: build/poll_mtx_mul.o | bin
	$(CC) $^ -o $@

bin/buf_mtx_mul: build/buf_mtx_mul.o | bin
	$(CC) $^ -o $@

bin/slot_mtx_mul: build/slot_mtx_mul.o | bin
	$(CC) $^ -o $@

bin/dma_mtx_mul_test: build/dma_mtx_mul_test.o | bin
	$(CC) $^ -o $@

bin/dma_mtx_mul_test_v2: build/dma_mtx_mul_test_v2.o | bin
	$(CC) $^ -o $@

bin/dma_mtx_mul_test_v3: build/dma_mtx_mul_test_v3.o | bin
	$(CC) $^ -o $@


build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

bin:
	mkdir -p bin

clean:
	rm -rf build bin