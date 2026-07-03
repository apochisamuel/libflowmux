CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Iinclude -Isrc
LDFLAGS ?=

ALL_SRC = $(wildcard src/common/*.c) $(wildcard src/protocol/*.c) \
          $(wildcard src/session/*.c) $(wildcard src/span/*.c) \
          $(wildcard src/segment/*.c) $(wildcard src/lease/*.c) \
          $(wildcard src/cursor/*.c) $(wildcard src/channel/*.c) \
          $(wildcard src/stream/*.c) $(wildcard src/replication/*.c) \
          $(wildcard src/migration/*.c) $(wildcard src/checkpoint/*.c) \
          $(wildcard src/retention/*.c) $(wildcard src/route/*.c) \
          $(wildcard src/backpressure/*.c)

ALL_OBJ = $(ALL_SRC:.c=.o)

.PHONY: all clean poc test fuzzers asan

all: libvaultmux.a

libvaultmux.a: $(ALL_OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

poc: tools/
tools/: tools/poc_stale_cursor_epoch.c libvaultmux.a
	$(CC) $(CFLAGS) -fsanitize=address -g -O1 -o $@ $< -L. -lvaultmux $(LDFLAGS)

test: tests/test_basic
	./tests/test_basic

tests/%: tests/%.c libvaultmux.a
	$(CC) $(CFLAGS) -g -O0 -o $@ $< -L. -lvaultmux $(LDFLAGS)

fuzzers: fuzz/stream_fuzzer fuzz/channel_fuzzer fuzz/session_fuzzer fuzz/migrate_fuzzer
fuzz/%: fuzz/%.c libvaultmux.a
	$(CC) $(CFLAGS) -fsanitize=fuzzer,address -g -O1 -o $@ $< -L. -lvaultmux $(LDFLAGS)

asan: CFLAGS += -fsanitize=address -g -O1 -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address
asan: clean all

clean:
	find src -name '*.o' -delete
	rm -f libvaultmux.a tools/
	rm -f tests/test_basic tests/test_protocol tests/test_subsystems tests/test_integration tests/test_full_workflow
	rm -f fuzz/stream_fuzzer fuzz/channel_fuzzer fuzz/session_fuzzer fuzz/migrate_fuzzer
