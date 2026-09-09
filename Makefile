CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -msse4.2 -mavx2 -mfma -O2

all: verify

verify: src/core/zamicore_verify.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Iinclude src/core/zamicore_verify.c -o build/verify
	./build/verify

clean:
	rm -rf build
