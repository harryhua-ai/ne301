CC ?= cc
REPO_ROOT = ../..

INC = -Ishim \
      -I$(REPO_ROOT)/Custom/Common/Lib/littlefs \
      -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
      -I$(REPO_ROOT)/Custom/Hal \
      -I$(REPO_ROOT)/Custom/Core/System \
      -I$(REPO_ROOT)/Custom/Common/Inc

STRICT_CFLAGS = -Wall -Wextra -Werror -std=c11 -O1 -g -D'__packed=__attribute__((packed))'
THIRD_PARTY_CFLAGS = -Wall -Wno-sign-compare -std=c11 -O1 -include stddef.h -D'__packed=__attribute__((packed))'

FIRST_PARTY_OBJS = test_storage_gate.o fake_flash.o storage_media_gate.o json_config_boot_gate.o
THIRD_PARTY_OBJS = lfs.o lfs_util.o nvs.o

.PHONY: test clean
test: test_storage_gate
	./test_storage_gate

test_storage_gate: $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)

%.o: %.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

storage_media_gate.o: $(REPO_ROOT)/Custom/Hal/storage_media_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

json_config_boot_gate.o: $(REPO_ROOT)/Custom/Core/System/json_config_boot_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

lfs.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

lfs_util.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs_util.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

nvs.o: $(REPO_ROOT)/Custom/Common/Lib/nvs/nvs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

clean:
	rm -f test_storage_gate *.o
