# =============================================================================
# Makefile — Issue #32 offline signing feasibility spike (host build)
#
# Builds the firmware's OWN mbedTLS 3.6.4 sources
# (Custom/Common/Lib/MbedTLS/library/*.c — the same wildcard set the firmware
# build uses, Appli/Makefile:296) with the firmware's real configuration
# (config-ccm-psk-tls1_2.h selected by Appli/Makefile:648, host-adapted by
# host_user_config.h) and runs the sign/verify test cases against them.
#
# The hardware ALT implementations (Custom/Common/Lib/MbedTLS/port/*.c) are
# deliberately NOT compiled here: they are STM32N6 PKA/CRYP/HASH/RTOS glue.
#
# Usage:  make          # build + run
#         make size     # object size table for verify-path objects
#         make clean
# =============================================================================
CC      ?= cc
REPO_ROOT := ../..
MBEDTLS   := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

# -Ishim FIRST so the firmware config's `#include "mem.h"` resolves to the
# host shim instead of Custom/Hal/mem.h; -I. so MBEDTLS_CONFIG_FILE finds
# host_user_config.h; then the firmware's own include dirs.
INC := -Ishim -I. -I$(MBEDTLS)/configs -I$(MBEDTLS)/include -I$(MBEDTLS)/library
DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS
CFLAGS_COMMON := -std=c11 -O2 -g -Wall -Wextra $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
LIB_OBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

.PHONY: all test size clean
all: test

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

test_sign_spike: test_sign_spike.o $(LIB_OBJS)
	$(CC) $(CFLAGS_COMMON) -o $@ $^

test_sign_spike.o: test_sign_spike.c | build
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

# Exit-code semantics: the test binary's status IS make's status (0 = all
# checks passed). run-output.txt stays the archived transcript.
test: test_sign_spike
	@./test_sign_spike > run-output.txt 2>&1; \
	rc=$$?; \
	cat run-output.txt; \
	exit $$rc

# Indicative footprint of the objects reachable from the ECDSA P-256 verify
# path (host compiler/arch — NOT device ROM/RAM numbers).
size: build
	@size $(LIB_OBJS) | sort -k1 -n | tail -40

clean:
	rm -rf build test_sign_spike
