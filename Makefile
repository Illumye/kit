# kit.h - build and test
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

EXAMPLE_SRC := $(wildcard examples/*.c)
EXAMPLE_BIN := $(patsubst examples/%.c,examples/%,$(EXAMPLE_SRC))

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(patsubst tests/%.c,tests/run_%,$(TEST_SRC))
TEST_ASAN:= $(patsubst tests/%.c,tests/run_%_asan,$(TEST_SRC))

# The portable overflow checks are dead code on gcc and clang, which have the
# builtins, and they are the only ones MSVC ever compiles. Running the same
# suite twice is what keeps the path this machine cannot reach honest.
TEST_BIN += tests/run_test_number_portable

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

.PHONY: all test test-asan test-all examples check-c11 check-namespace check-cxx \
        check-examples check-windows fuzz fuzz-build clean

all: test

# --- tests --------------------------------------------------------------------

tests/run_%: tests/%.c kit.h tests/utest.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

tests/run_%_asan: tests/%.c kit.h tests/utest.h
	$(CC) $(CFLAGS) -O1 $(SAN) $< -o $@ $(LDLIBS)

tests/run_test_number_portable: tests/test_number.c kit.h tests/utest.h
	$(CC) $(CFLAGS) -DKIT_NO_OVERFLOW_BUILTINS $< -o $@ $(LDLIBS)

test: $(TEST_BIN)
	@rc=0; for t in $(TEST_BIN); do ./$$t || rc=1; done; exit $$rc

# LeakSanitizer ships with AddressSanitizer on Linux only; asking for it
# anywhere else aborts the process before the first test runs.
LEAK_CHECK := $(shell [ "`uname -s`" = Linux ] && echo 1 || echo 0)

test-asan: $(TEST_ASAN)
	@rc=0; for t in $(TEST_ASAN); do \
		ASAN_OPTIONS=detect_leaks=$(LEAK_CHECK) UBSAN_OPTIONS=print_stacktrace=1 ./$$t || rc=1; \
	done; exit $$rc

# The header requests the POSIX symbols it needs, so a strict -std=c11 with no
# GNU extensions must compile clean. KIT_NO_VEC_MATH must also stay buildable,
# and reaching the header twice in one translation unit must not duplicate the
# implementation.
check-c11:
	@echo '#define KIT_IMPLEMENTATION' > .compile-check.c
	@echo '#include "kit.h"' >> .compile-check.c
	@echo '#include "kit.h"' >> .compile-check.c
	@echo 'int main(void) { return 0; }' >> .compile-check.c
	$(CC) -std=c11 $(WARNINGS) -Werror .compile-check.c -o /dev/null $(LDLIBS)
	$(CC) -std=c11 $(WARNINGS) -Werror -DKIT_NO_VEC_MATH .compile-check.c -o /dev/null
	@rm -f .compile-check.c
	@echo "strict C11 compile: ok"

test-all: check-c11 check-namespace check-cxx test test-asan check-examples

# --- namespace -----------------------------------------------------------------
# Two sides of the same promise. check_namespace.sh fails if the header adds a
# macro, symbol, type or enumerator without the kit prefix. check_coexistence.c
# defines the names a real project already uses, next to syslog.h, and has to
# compile: the old unprefixed header produced 22 errors on it.

check-namespace:
	@CC=$(CC) ./tests/check_namespace.sh kit.h
	@$(CC) -std=c11 $(WARNINGS) -Werror tests/check_coexistence.c -o tests/run_coexistence $(LDLIBS)
	@./tests/run_coexistence && echo "coexistence: ok"

# --- C++ ---------------------------------------------------------------------
# The header has to be usable from C++ with the implementation included, not
# only its declarations. This is what keeps the extern "C" guard, the
# language-neutral struct literals and the explicit allocation casts honest.

tests/run_test_cxx: tests/test_cxx.cpp kit.h tests/utest.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

check-cxx: tests/run_test_cxx
	@./tests/run_test_cxx

# --- fuzzing -------------------------------------------------------------------
# libFuzzer, so clang whatever CC is. Each target asserts its own invariants
# and the sanitizers catch the rest. Not part of test-all: it is a search, not
# a pass or fail, and it takes as long as you let it.

tests/fuzz/run_%: tests/fuzz/%.c kit.h tests/fuzz/fuzz_input.h
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

tests/win_%.exe: tests/%.c kit.h tests/utest.h
	$(MINGW) $(CFLAGS) $< -o $@

# Not part of test-all: it needs a cross compiler that most machines lack.
check-windows: $(WIN_BIN)
	@rc=0; for t in $(WIN_BIN); do WINEDEBUG=-all $(WINE) $$t || rc=1; done; \
	 echo "windows: done"; exit $$rc

# --- examples -----------------------------------------------------------------

examples: $(EXAMPLE_BIN)

examples/%: examples/%.c kit.h
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

# The examples are the integration tests: each one drives several modules at
# once, the way a real program would, which no unit test does. They are run
# here and their output is checked, so an example that stops working stops the
# build. check_examples.sh additionally fails if any public name is shown
# nowhere.
check-examples: $(EXAMPLE_BIN)
	@./examples/build --clean > /dev/null 2>&1
	@./examples/build -r 2>&1 | grep -q 'hello, world'
	@./examples/build    2>&1 | grep -q '0 file(s) compiled'
	@touch examples/demo/greet.h
	@./examples/build    2>&1 | grep -q '0 file(s) compiled'
# A real change to the same header does rebuild. The copy is put back
# whatever happens, so a failing check never leaves the source modified.
	@cp examples/demo/greet.h build/greet.h.kept
	@printf '/* changed by check-examples */\n' >> examples/demo/greet.h
	@./examples/build 2>&1 | grep -q '2 file(s) compiled'; rc=$$?; \
	 cp build/greet.h.kept examples/demo/greet.h; rm -f build/greet.h.kept; \
	 exit $$rc
	@./examples/build --clean > /dev/null 2>&1
	@./examples/peek --help > /dev/null
	@./examples/peek examples/demo/app.conf | grep -q '|# The server the|'
	@./examples/peek -n16 -o=16 examples/demo/prose.txt | grep -q '^00000010'
	@./examples/peek examples/demo 2>&1 | grep -q 'is a directory'
	@./examples/peek --sums -n 0 examples/demo/app.conf | grep -q 'sha256  d2cadd9db49147506d8da0521ffb5fef87ddff5502ccadf4a6b500de42fea872'
	@./examples/config examples/demo/app.conf | grep -q '^port        8080'
	@./examples/config examples/demo/app.conf --list | grep -q 'paths.log'
	@./examples/config examples/demo/broken.conf 2>&1 | grep -q 'expected a whole number'
	@./examples/wordfreq --top 3 examples/demo/prose.txt | grep -q 'most common: errors'
	@./examples/wordfreq --bench 5 --top 1 examples/demo/prose.txt | grep -q 'counting.*5 samples'
	@./examples/tree --depth 2 examples/demo | grep -q 'app.conf'
	@./examples/tree --depth 2 --largest 2 examples/demo | grep -q 'largest 2'
	@./examples/orbit --bodies 3 --steps 50 | grep -q 'spin axis'
	@./examples/runner -- echo hello | grep -q '^hello$$'
	@./examples/runner -- sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo line $$i; done; exit 7' 2>&1 \
	   | grep -q '2 earlier line'
	@./examples/runner --check no-such-program 2>&1 | grep -q 'missing'
	@rm -rf build/journal
	@./examples/journal --dir build/journal --limit 700 --lines 20 --quiet 2>&1 \
	   | grep -q '20 lines written'
	@rm -rf build/journal
	@./tests/check_examples.sh

clean:
	rm -f $(TEST_BIN) $(TEST_ASAN) $(WIN_BIN) $(FUZZ_BIN) tests/run_test_cxx tests/run_coexistence \
	      $(EXAMPLE_BIN) .compile-check.c .cxx-check.cpp
	rm -rf build
	rm -rf utest-tmp-*
