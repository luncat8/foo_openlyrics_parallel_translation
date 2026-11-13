# Developer checks that do not need Windows or foobar2000.
#
# The plugin itself is built with MSBuild (see build/foo_openlyrics.sln) and its MVTF
# tests run inside foobar2000. The bundle parser, however, is deliberately portable, so
# everything that can be checked without Windows is checked here. CI runs `make check`.

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
BUILD    ?= build/portable

CHECKER  := $(BUILD)/linguamap_check

CORE_SRC := src/lyric_map.cpp
CHECK_SRC := tools/linguamap_check/main.cpp

EXAMPLES := $(wildcard docs/examples/*.lmz.txt)
VALID    := $(EXAMPLES) $(wildcard tests/data/linguamap/valid/*.lmz.txt)
INVALID  := $(wildcard tests/data/linguamap/invalid/*.lmz.txt)

.PHONY: all check check-examples check-invalid check-palette clean

all: check

$(CHECKER): $(CORE_SRC) $(CHECK_SRC) src/lyric_map.h src/colorization.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $(CORE_SRC) $(CHECK_SRC)

# Verifies that the palette in src/colorization.h is exactly the one the LinguaMap
# repository publishes. Skipped when the reference file is not checked in.
check-palette:
	@if [ -f tests/data/colorization.json ]; then \
		./tools/check_palette.py tests/data/colorization.json src/colorization.h; \
	else \
		echo "skip: tests/data/colorization.json is not present"; \
	fi

# Accepts every valid bundle and proves each parsed map is self-consistent.
check-examples: $(CHECKER)
	@set -e; \
	for f in $(VALID); do $(CHECKER) "$$f" >/dev/null; done; \
	echo "$$(echo $(VALID) | wc -w) valid bundle(s) accepted"

# Proves that each rejection case is rejected (with an explanation, not silently).
check-invalid: $(CHECKER)
	@set -e; \
	for f in $(INVALID); do $(CHECKER) --reject "$$f" >/dev/null; done; \
	echo "$$(echo $(INVALID) | wc -w) invalid bundle(s) rejected"

check: check-palette check-examples check-invalid

clean:
	rm -rf $(BUILD)
