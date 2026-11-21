CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?= -lm
BUILD ?= build
SHORT_PROGRAMS := lab1 lab2 lab3 lab4 lab5 lab6 lab7 lab8 lab9
.PHONY: all clean check memcheck
all: $(SHORT_PROGRAMS) lab10
$(BUILD):
	mkdir -p $@
$(BUILD)/lab%: lab%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
$(SHORT_PROGRAMS): lab%: $(BUILD)/lab0%
	cp "$<" "$@"
lab10: $(BUILD)/lab10
	cp "$<" "$@"
check: all
	sh tests.sh "$(BUILD)"
memcheck: all
	CHECK_MEMORY=1 sh tests.sh "$(BUILD)"
clean:
	rm -rf $(BUILD)
	rm -f $(SHORT_PROGRAMS) lab10
