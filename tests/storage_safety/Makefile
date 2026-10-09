# Host error-injection regression for issue #37 (storage/config non-destructive boot).
#
# Links the REAL firmware decision logic with the REAL littlefs and NVS
# libraries against a fault-injecting RAM flash:
#   - Custom/Hal/storage_safety.c        (probe/format/blank-check decisions)
#   - Custom/Core/System/json_config_boot_policy.c (config boot state policy)
#   - Custom/Common/Lib/littlefs/lfs.c   (filesystem under test)
#   - Custom/Common/Lib/nvs/nvs.c        (NVS library under test)
#
# Usage: make -C tests/storage_safety test

CC ?= cc
REPO_ROOT = ../..

INC = -Ishim \
      -I$(REPO_ROOT)/Custom/Common/Lib/littlefs \
      -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
      -I$(REPO_ROOT)/Custom/Hal \
      -I$(REPO_ROOT)/Custom/Core/System

# Strict flags for first-party decision logic and the test harness.
STRICT_CFLAGS = -Wall -Wextra -Werror -std=c11 -O1 -g
# Third-party firmware libraries: keep warnings, add the one host-portability
# shim they need on the target toolchain anyway (stddef.h via force-include
# for NVS's offsetof; the target compiler sees it transitively).
THIRD_PARTY_CFLAGS = -Wall -Wno-sign-compare -std=c11 -O1 -include stddef.h

FIRST_PARTY_OBJS = test_storage_safety.o fake_bd.o \
    storage_safety.o json_config_boot_policy.o
THIRD_PARTY_OBJS = lfs.o lfs_util.o nvs.o

.PHONY: test clean
test: test_storage_safety
	./test_storage_safety

test_storage_safety: $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)

%.o: %.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

storage_safety.o: $(REPO_ROOT)/Custom/Hal/storage_safety.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

json_config_boot_policy.o: $(REPO_ROOT)/Custom/Core/System/json_config_boot_policy.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

lfs.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

lfs_util.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs_util.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

nvs.o: $(REPO_ROOT)/Custom/Common/Lib/nvs/nvs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

clean:
	rm -f test_storage_safety *.o
