CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I. \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/tests/app_installer

DEFS := -D_GNU_SOURCE -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)
LEGACY_CFLAGS := -std=c11 -O1 -g -Wno-everything $(INC) $(DEFS)

V3_SIGNING_KEY ?= build/test-v3-signing-key.pem
V3_FOREIGN_SIGNING_KEY ?= build/test-v3-foreign-signing-key.pem

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

CORE_OBJS := app_admission.o app_trust_anchor.o generic_math.o

.PHONY: test clean
test: test_app_trust_profile $(MBOBJS)
	python3 check_trust_profile_builds.py

test_app_trust_profile: test_app_trust_profile.o app_trust_anchor_production.o app_admission.o generic_math.o $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_admission_v3.o: test_app_admission_v3.c build/v3_vectors.h
	$(CC) $(STRICT_CFLAGS) -Ibuild -c $< -o $@

test_app_admission_gate.o: test_app_admission_gate.c p2_vectors.h anchor_vectors.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

buffer_test_alloc.o: buffer_test_alloc.c $(REPO_ROOT)/Custom/Core/Data/buffer_mgr.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

test_app_trust_profile.o: test_app_trust_profile.c $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_trust_anchor_production.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.h
	$(CC) $(STRICT_CFLAGS) -DAPP_TRUST_PROFILE=APP_TRUST_PROFILE_PRODUCTION -c $< -o $@

build/test-v3-signing-key.pem: $(REPO_ROOT)/tests/sign_spike/vectors/ecdsa_p256_spike_testkey1.pem | build
	cp $< $@
	chmod 600 $@

build/test-v3-foreign-signing-key.pem: $(REPO_ROOT)/tests/sign_spike/vectors/ecdsa_p256_spike_testkey2.pem | build
	cp $< $@
	chmod 600 $@

build/v3_vectors.h: gen_v3_vectors.py anchor_keygen.py $(V3_SIGNING_KEY) $(V3_FOREIGN_SIGNING_KEY) | build
	python3 gen_v3_vectors.py --key "$(V3_SIGNING_KEY)" --foreign-key "$(V3_FOREIGN_SIGNING_KEY)" --output $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_trust_anchor.o: $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.c $(REPO_ROOT)/Custom/Services/AppHost/app_trust_anchor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_app_trust_profile *.o
