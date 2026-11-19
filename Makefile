CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lm
BUILD ?= build
PROGRAMS := $(addprefix $(BUILD)/,lab01 lab02 lab03 lab04 lab05 lab06 lab07 lab08 lab09 lab10)
.PHONY: all clean check memcheck
all: $(PROGRAMS)
$(BUILD):
	mkdir -p $@
$(BUILD)/lab04: lab04.c string_type.c string_type.h common.c common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) lab04.c string_type.c common.c $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/lab%: lab%.c common.c common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< common.c $(LDFLAGS) $(LDLIBS) -o $@
check: all
	sh tests.sh "$(BUILD)"
memcheck: all
	sh tests.sh "$(BUILD)" --valgrind
clean:
	rm -rf $(BUILD)
