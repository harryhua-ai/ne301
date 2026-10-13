CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_admission \
       -I$(REPO_ROOT)/tests/app_host_session \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Core/Video \
       -I$(REPO_ROOT)/Custom/Core/System \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Services/Video \
       -I$(REPO_ROOT)/Custom/Services/AI \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc \
       -I$(REPO_ROOT)/BSP/Utilities/Fonts \
       -I. \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS \
        -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

CORE_OBJS := app_host_session.o app_host_control.o app_state_store.o \
             app_installer.o app_admission.o app_host_validate.o generic_math.o

PTHREAD_FLAGS := -pthread

.PHONY: test clean
test: test_app_host_control
	./test_app_host_control

test_app_host_control: test_app_host_control.o $(CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -o $@ $^

test_app_host_control.o: test_app_host_control.c $(REPO_ROOT)/tests/app_host_session/session_vectors.h $(REPO_ROOT)/tests/app_host_control/control_vectors.h $(REPO_ROOT)/tests/app_installer/mem_io_backend.h
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

app_host_session.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_control.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.h
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
	rm -rf build test_app_host_control *.o
