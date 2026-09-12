# utils.h - build and test
#
#   make test        run the suite
#   make test-asan   run it under AddressSanitizer + UndefinedBehaviorSanitizer
#   make test-all    both, plus a strict-C11 and a C++-free compile check
#   make examples    build the example programs
#   make clean

CC       ?= cc
WARNINGS  = -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wcast-qual \
            -Wstrict-prototypes -Wwrite-strings
CFLAGS   ?= -std=c11 -O2 -g $(WARNINGS)
LDLIBS    = -lm

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(patsubst tests/%.c,tests/run_%,$(TEST_SRC))
TEST_ASAN:= $(patsubst tests/%.c,tests/run_%_asan,$(TEST_SRC))

SAN = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

.PHONY: all test test-asan test-all examples check-c11 clean

all: test

# --- tests --------------------------------------------------------------------

tests/run_%: tests/%.c utils.h tests/utest.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

tests/run_%_asan: tests/%.c utils.h tests/utest.h
	$(CC) $(CFLAGS) -O1 $(SAN) $< -o $@ $(LDLIBS)

test: $(TEST_BIN)
	@rc=0; for t in $(TEST_BIN); do ./$$t || rc=1; done; exit $$rc

test-asan: $(TEST_ASAN)
	@rc=0; for t in $(TEST_ASAN); do \
		ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./$$t || rc=1; \
	done; exit $$rc

# The header requests the POSIX symbols it needs, so a strict -std=c11 with no
# GNU extensions must compile clean. UTILS_NO_VEC_MATH must also stay buildable.
check-c11:
	@echo '#define UTILS_IMPLEMENTATION' > .compile-check.c
	@echo '#include "utils.h"' >> .compile-check.c
	@echo 'int main(void) { return 0; }' >> .compile-check.c
	$(CC) -std=c11 $(WARNINGS) -Werror .compile-check.c -o /dev/null $(LDLIBS)
	$(CC) -std=c11 $(WARNINGS) -Werror -DUTILS_NO_VEC_MATH .compile-check.c -o /dev/null
	@rm -f .compile-check.c
	@echo "strict C11 compile: ok"

test-all: check-c11 test test-asan

# --- examples -----------------------------------------------------------------

examples: example_cli

example_cli: example_cli.c utils.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TEST_BIN) $(TEST_ASAN) example_cli .compile-check.c
	rm -f utest-tmp-*
