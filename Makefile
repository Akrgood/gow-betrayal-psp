# Convenience wrapper. The real PSP build rules live in psp/Makefile.
PSPDEV ?= $(HOME)/pspdev
export PATH := $(PSPDEV)/bin:$(PATH)

CXX ?= c++
PYTHON ?= python3

.PHONY: all build clean test

all: build

build:
	$(MAKE) -C psp -j4

clean:
	$(MAKE) -C psp clean
	rm -f psp/GamePsp.cpp psp/locale_strings.inc psp/*.d
	rm -rf psp/__pycache__

# Host-side checks. No PSP toolchain is required for these.
test:
	$(MAKE) -C psp locale_strings.inc
	$(CXX) -std=c++17 -O2 -Wall -Wextra -fsanitize=undefined \
		-Isrc -Ipsp psp/test_blit.cpp src/gfx.cpp -o /tmp/gow-test-blit
	/tmp/gow-test-blit
	$(CXX) -std=c++17 -O2 -Wall -Wextra -fsanitize=undefined \
		-Isrc -Ipsp psp/test_locales.cpp psp/locale_psp.cpp -o /tmp/gow-test-locales
	/tmp/gow-test-locales
