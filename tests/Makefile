# Host-side tests for the game rules. Needs gcc (or clang) and node.
#
#   make test    unit tests
#   make trace   compare the C and JavaScript traces
#   make check   both

CC      ?= gcc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Werror
BUILD    = build
GAME     = ../game
NODE    ?= node

all: $(BUILD)/test_game $(BUILD)/trace

$(BUILD)/test_game: test_game.c autoplay.h $(GAME)/game.c $(GAME)/game.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(GAME) -o $@ test_game.c $(GAME)/game.c

$(BUILD)/trace: trace_main.c autoplay.h $(GAME)/game.c $(GAME)/game.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(GAME) -o $@ trace_main.c $(GAME)/game.c

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_game
	./$(BUILD)/test_game

trace: $(BUILD)/trace
	./$(BUILD)/trace > $(BUILD)/trace_c.txt
	$(NODE) trace.js > $(BUILD)/trace_js.txt
	@cmp $(BUILD)/trace_c.txt $(BUILD)/trace_js.txt && \
	    echo "C and JavaScript traces match ($$(wc -l < $(BUILD)/trace_c.txt) lines)"

check: test trace

clean:
	rm -rf $(BUILD)

.PHONY: all test trace check clean
