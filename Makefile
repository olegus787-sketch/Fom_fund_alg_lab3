CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lm
BUILD ?= build
PROGRAMS := $(addprefix $(BUILD)/,lab01 lab02 lab03 lab04 lab05 lab06 lab07 lab08 lab09 lab10)
.PHONY: all clean check memcheck
all: $(PROGRAMS)
$(BUILD):
	mkdir -p $@
$(BUILD)/lab%: lab%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
check: all
	sh tests.sh "$(BUILD)"
memcheck: all
	CHECK_MEMORY=1 sh tests.sh "$(BUILD)"
clean:
	rm -rf $(BUILD)
