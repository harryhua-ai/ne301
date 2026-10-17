CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS
BUILD := build

INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs \
       -I$(REPO_ROOT)/tests/sign_spike

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS
STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
CORE_OBJS := app_sidecar_verify.o app_admission.o app_trust_anchor.o generic_math.o
LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix $(BUILD)/,$(notdir $(LIB_SRCS:.c=.o)))

.PHONY: test clean
test: test_app25_asset
	node audit-origin-input.mjs
	./test_app25_asset $(REPO_ROOT)/tests/sidecar-origin-study/input/line-crossing.neui $(REPO_ROOT)/tests/sidecar-origin-study/input/test-publisher.spki.der

test_app25_asset: test_app25_asset.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app25_asset.o: test_app25_asset.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_sidecar_verify.o: $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_trust_anchor.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(MBEDTLS)/library/%.c | $(BUILD)
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf $(BUILD) test_app25_asset *.o
