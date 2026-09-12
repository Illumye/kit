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
LDLIBS    = -lm -pthread

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(patsubst tests/%.c,tests/run_%,$(TEST_SRC))
TEST_ASAN:= $(patsubst tests/%.c,tests/run_%_asan,$(TEST_SRC))

SAN = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all

# Windows cross build. The _WIN32 branches are otherwise never compiled at all.
# Needs gcc-mingw-w64-x86-64, and wine to run the result.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra

FUZZ_SRC  := $(wildcard tests/fuzz/fuzz_*.c)
FUZZ_BIN  := $(patsubst tests/fuzz/%.c,tests/fuzz/run_%,$(FUZZ_SRC))
FUZZ_CC   ?= clang
FUZZ_SECS ?= 15

MINGW    ?= x86_64-w64-mingw32-gcc
WINE     ?= wine
WIN_BIN  := $(patsubst tests/%.c,tests/win_%.exe,$(TEST_SRC))

.PHONY: all test test-asan test-all examples check-c11 check-cxx \
        check-examples check-windows fuzz fuzz-build clean

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
# GNU extensions must compile clean. UTILS_NO_VEC_MATH must also stay buildable,
# and reaching the header twice in one translation unit must not duplicate the
# implementation.
check-c11:
	@echo '#define UTILS_IMPLEMENTATION' > .compile-check.c
	@echo '#include "utils.h"' >> .compile-check.c
	@echo '#include "utils.h"' >> .compile-check.c
	@echo 'int main(void) { return 0; }' >> .compile-check.c
	$(CC) -std=c11 $(WARNINGS) -Werror .compile-check.c -o /dev/null $(LDLIBS)
	$(CC) -std=c11 $(WARNINGS) -Werror -DUTILS_NO_VEC_MATH .compile-check.c -o /dev/null
	@rm -f .compile-check.c
	@echo "strict C11 compile: ok"

test-all: check-c11 check-cxx test test-asan check-examples

# --- C++ ---------------------------------------------------------------------
# The header has to be usable from C++ with the implementation included, not
# only its declarations. This is what keeps the extern "C" guard, the
# language-neutral struct literals and the explicit allocation casts honest.

tests/run_test_cxx: tests/test_cxx.cpp utils.h tests/utest.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

check-cxx: tests/run_test_cxx
	@./tests/run_test_cxx

# --- fuzzing -------------------------------------------------------------------
# libFuzzer, so clang whatever CC is. Each target asserts its own invariants
# and the sanitizers catch the rest. Not part of test-all: it is a search, not
# a pass or fail, and it takes as long as you let it.

tests/fuzz/run_%: tests/fuzz/%.c utils.h tests/fuzz/fuzz_input.h
	$(FUZZ_CC) -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
	           -fno-sanitize-recover=all $(WARNINGS) $< -o $@

fuzz-build: $(FUZZ_BIN)

# A bounded run, short enough for CI. Give it FUZZ_SECS=600 to go looking.
fuzz: $(FUZZ_BIN)
	@rc=0; for f in $(FUZZ_BIN); do \
		echo "== $$f ($(FUZZ_SECS)s)"; \
		mkdir -p $$f.corpus; \
		$$f $$f.corpus -max_total_time=$(FUZZ_SECS) -print_final_stats=1 \
		    -rss_limit_mb=2048 || rc=1; \
	done; exit $$rc

# --- Windows -------------------------------------------------------------------

tests/win_%.exe: tests/%.c utils.h tests/utest.h
	$(MINGW) $(CFLAGS) $< -o $@

# Not part of test-all: it needs a cross compiler that most machines lack.
check-windows: $(WIN_BIN)
	@rc=0; for t in $(WIN_BIN); do WINEDEBUG=-all $(WINE) $$t || rc=1; done; \
	 echo "windows: done"; exit $$rc

# --- examples -----------------------------------------------------------------

examples: examples/cli examples/build

examples/cli: examples/cli.c utils.h
	$(CC) $(CFLAGS) $< -o $@

examples/build: examples/build.c utils.h
	$(CC) $(CFLAGS) $< -o $@

# The example build tool is the integration test: it drives the option parser,
# the filesystem layer, the temporary allocator, the command runner and the
# logger at once, which no unit test does. The logger writes to stderr, hence
# the redirections.
check-examples: examples/build examples/cli
	@./examples/build --clean > /dev/null 2>&1
	@./examples/build -r 2>&1 | grep -q 'hello, world'
	@./examples/build    2>&1 | grep -q '0 file(s) compiled'
	@touch examples/demo/greet.h
	@./examples/build    2>&1 | grep -q '2 file(s) compiled'
	@./examples/cli --help > /dev/null
	@./examples/build --clean > /dev/null 2>&1
	@echo "examples: ok"

clean:
	rm -f $(TEST_BIN) $(TEST_ASAN) $(WIN_BIN) $(FUZZ_BIN) tests/run_test_cxx \
	      examples/cli examples/build .compile-check.c .cxx-check.cpp
	rm -rf build
	rm -rf utest-tmp-*
