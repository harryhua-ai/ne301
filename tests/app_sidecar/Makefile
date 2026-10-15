CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I. \
       -Ibuild \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"'

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

.PHONY: test clean
test: build/vectors test_app_sidecar
	./test_app_sidecar

test_app_sidecar: test_app_sidecar.o app_sidecar_verify.o $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

build/vectors: gen_sidecar_fixtures.py | build
	python3 gen_sidecar_fixtures.py --output build
	touch $@

test_app_sidecar.o: test_app_sidecar.c build/vectors build/sidecar_vectors.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_sidecar_verify.o: $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.c $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_verify.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) -std=c11 -O1 -g -w $(INC) $(DEFS) -c $< -o $@

clean:
	rm -rf build test_app_sidecar *.o
