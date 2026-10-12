CC ?= cc
REPO_ROOT = ../..

INC = -Ishim \
      -I$(REPO_ROOT)/Custom/Common/Lib/littlefs \
      -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
      -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
      -I$(REPO_ROOT)/Custom/Hal \
      -I$(REPO_ROOT)/Custom/Core/System \
      -I$(REPO_ROOT)/Custom/Core/Security \
      -I$(REPO_ROOT)/Custom/Core/Data \
      -I$(REPO_ROOT)/Custom/Services/Web \
      -I$(REPO_ROOT)/Custom/Services/Web/api \
      -I$(REPO_ROOT)/Custom/Services/Device \
      -I$(REPO_ROOT)/Custom/Services/OTA \
      -I$(REPO_ROOT)/Custom/Services/Communication \
      -I$(REPO_ROOT)/Custom/Common/Inc \
      -I$(REPO_ROOT)/Custom/Common/Utils \
      -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc

STRICT_CFLAGS = -Wall -Wextra -Werror -std=c11 -O1 -g -D'__packed=__attribute__((packed))'
THIRD_PARTY_CFLAGS = -Wall -Wno-sign-compare -std=c11 -O1 -include stddef.h -D'__packed=__attribute__((packed))'
LEGACY_CFLAGS = -std=c11 -O1 -g -Wno-everything -DSTM32N657xx=1 -D'__packed=__attribute__((packed))'

FIRST_PARTY_OBJS = test_storage_gate.o fake_flash.o storage_media_gate.o json_config_boot_gate.o storage_nvs_session.o
THIRD_PARTY_OBJS = lfs.o lfs_util.o nvs.o

E2E_REAL_OBJS = e2e_storage_boot.o e2e_backend.o json_config_nvs.o json_config_mgr.o json_config_utils.o json_config_boot_gate.o auth_mgr.o api_device_module.o cJSON.o
E2E_LINK_OBJS = fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o gen_stubs.o
E2E_STUB_INPUT_OBJS = $(E2E_REAL_OBJS) fake_flash.o storage_media_gate.o storage_nvs_session.o nvs.o lfs.o lfs_util.o
E2E_OBJS = $(E2E_REAL_OBJS) $(E2E_LINK_OBJS)

.PHONY: test clean
test: test_storage_gate test_storage_e2e
	./test_storage_gate
	./test_storage_e2e

test_storage_gate: $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(FIRST_PARTY_OBJS) $(THIRD_PARTY_OBJS)

test_storage_e2e: $(E2E_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $(E2E_OBJS)

%.o: %.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

storage_media_gate.o: $(REPO_ROOT)/Custom/Hal/storage_media_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

storage_nvs_session.o: $(REPO_ROOT)/Custom/Hal/storage_nvs_session.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

json_config_boot_gate.o: $(REPO_ROOT)/Custom/Core/System/json_config_boot_gate.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

json_config_nvs.o: $(REPO_ROOT)/Custom/Core/System/json_config_nvs.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

json_config_mgr.o: $(REPO_ROOT)/Custom/Core/System/json_config_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

auth_mgr.o: $(REPO_ROOT)/Custom/Core/Security/auth_mgr.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

api_device_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_device_module.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

json_config_utils.o: $(REPO_ROOT)/Custom/Core/System/json_config_utils.c
	$(CC) $(LEGACY_CFLAGS) $(INC) -c $< -o $@

lfs.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

lfs_util.o: $(REPO_ROOT)/Custom/Common/Lib/littlefs/lfs_util.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

nvs.o: $(REPO_ROOT)/Custom/Common/Lib/nvs/nvs.c
	$(CC) $(THIRD_PARTY_CFLAGS) $(INC) -c $< -o $@

gen_stubs.c: $(E2E_REAL_OBJS) gen_stubs.py FORCE
	@python3 gen_stubs.py $(E2E_STUB_INPUT_OBJS)

FORCE:

gen_stubs.o: gen_stubs.c
	$(CC) $(STRICT_CFLAGS) $(INC) -c $< -o $@

clean:
	rm -f test_storage_gate test_storage_e2e *.o gen_stubs.c
