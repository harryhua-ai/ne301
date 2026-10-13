CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_admission \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/tests/app_host_session \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I. \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

CORE_OBJS := app_host_session.o app_installer.o app_admission.o app_host_validate.o generic_math.o

PTHREAD_FLAGS := -pthread

.PHONY: test clean
test: test_app_host_session
	./test_app_host_session

test_app_host_session: test_app_host_session.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -o $@ $^

test_app_host_session.o: test_app_host_session.c $(REPO_ROOT)/tests/app_installer/installer_vectors.h $(REPO_ROOT)/tests/app_admission/anchor_vectors.h $(REPO_ROOT)/tests/app_host_session/session_vectors.h $(REPO_ROOT)/tests/app_installer/mem_io_backend.h
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

app_host_session.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_validate.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_app_host_session *.o
