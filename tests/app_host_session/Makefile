CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

DEVSHIM := $(REPO_ROOT)/tests/app_host_session/devshim

INC := -I$(DEVSHIM) \
       -I$(REPO_ROOT)/tests/app_installer \
       -I$(REPO_ROOT)/tests/app_admission \
       -I$(REPO_ROOT)/tests/sign_spike \
       -I$(REPO_ROOT)/tests/app_host_session \
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
       -I$(MBEDTLS)/configs \
       -Ibuild

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS \
        -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS)

DEV_STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
                     $(INC) $(DEFS)

LIB_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
MBOBJS := $(addprefix build/,$(notdir $(LIB_SRCS:.c=.o)))

CORE_OBJS := app_host_session.o app_host_control.o app_state_store.o app_installer.o app_admission.o app_host_validate.o app_report_service.o app_report_policy.o generic_math.o

V3_SIGNING_KEY ?= build/test-v3-signing-key.pem
V3_FOREIGN_SIGNING_KEY ?= build/test-v3-foreign-signing-key.pem
V3_VECTORS := build/v3_vectors.h
V4_VECTORS := build/v4_vectors.h

DEV_CORE_OBJS := video_ai_node.o app_host_session_device.o dev_seam.o dev_host_env.o cJSON.o

PTHREAD_FLAGS := -pthread

.PHONY: test clean
test: test_app_host_session
	./test_app_host_session

test_app_host_session: test_app_host_session.o $(CORE_OBJS) $(DEV_CORE_OBJS) $(MBOBJS)
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -o $@ $^

test_app_host_session.o: test_app_host_session.c $(REPO_ROOT)/tests/app_installer/installer_vectors.h $(REPO_ROOT)/tests/app_admission/anchor_vectors.h $(REPO_ROOT)/tests/app_host_session/session_vectors.h $(REPO_ROOT)/tests/app_installer/mem_io_backend.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service_device.h $(DEVSHIM)/dev_seam.h $(V3_VECTORS) $(V4_VECTORS) $(REPO_ROOT)/tests/app_native_vnext/fixtures/app-pr37/live-only.neapp.v4 $(REPO_ROOT)/tests/app_native_vnext/fixtures/app-pr37/enable.neapp.v4 $(REPO_ROOT)/tests/app_native_vnext/fixtures/app-pr37/dev-publisher.spki.der
	$(CC) $(STRICT_CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

app_host_session.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service_device.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_control.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_control.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_state_store.o: $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.c $(REPO_ROOT)/Custom/Services/AppHost/app_state_store.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_installer.o: $(REPO_ROOT)/Custom/Services/AppHost/app_installer.c $(REPO_ROOT)/Custom/Services/AppHost/app_installer.h $(REPO_ROOT)/Custom/Services/AppHost/app_sidecar_lifecycle.c $(REPO_ROOT)/Custom/Services/AppHost/app_native_vnext_floor.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_admission.o: $(REPO_ROOT)/Custom/Services/AppHost/app_admission.c $(REPO_ROOT)/Custom/Services/AppHost/app_admission.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_host_validate.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_validate.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_service.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_policy.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_policy.h $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

generic_math.o: $(REPO_ROOT)/Custom/Common/Utils/generic_math.c
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

video_ai_node.o: $(REPO_ROOT)/Custom/Core/Video/video_ai_node.c $(REPO_ROOT)/Custom/Core/Video/video_ai_node.h
	$(CC) $(DEV_STRICT_CFLAGS) -include $(DEVSHIM)/dev_seam_decls.h -c $< -o $@

app_host_session_device.o: $(REPO_ROOT)/Custom/Services/AppHost/app_host_session_device.c $(REPO_ROOT)/Custom/Services/AppHost/app_host_session_device.h
	$(CC) $(DEV_STRICT_CFLAGS) -c $< -o $@

dev_seam.o: devshim/dev_seam.c devshim/dev_seam.h
	$(CC) $(DEV_STRICT_CFLAGS) -c $< -o $@

dev_host_env.o: devshim/dev_host_env.c devshim/cmsis_os2.h devshim/stm32n6xx_hal.h
	$(CC) $(DEV_STRICT_CFLAGS) $(PTHREAD_FLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) -std=c11 -O1 -g -w $(INC) $(DEFS) -c $< -o $@

build:
	mkdir -p build

build/test-v3-signing-key.pem: $(REPO_ROOT)/tests/sign_spike/vectors/ecdsa_p256_spike_testkey1.pem | build
	cp $< $@
	chmod 600 $@

build/test-v3-foreign-signing-key.pem: $(REPO_ROOT)/tests/sign_spike/vectors/ecdsa_p256_spike_testkey2.pem | build
	cp $< $@
	chmod 600 $@

$(V3_VECTORS): $(REPO_ROOT)/tests/app_admission/gen_v3_vectors.py $(REPO_ROOT)/tests/app_admission/anchor_keygen.py $(V3_SIGNING_KEY) $(V3_FOREIGN_SIGNING_KEY) | build
	python3 $(REPO_ROOT)/tests/app_admission/gen_v3_vectors.py --key "$(V3_SIGNING_KEY)" --foreign-key "$(V3_FOREIGN_SIGNING_KEY)" --output $@

$(V4_VECTORS): $(REPO_ROOT)/tests/app_admission/gen_v4_vectors.py $(REPO_ROOT)/tests/app_admission/anchor_keygen.py $(V3_SIGNING_KEY) $(V3_FOREIGN_SIGNING_KEY) | build
	python3 $(REPO_ROOT)/tests/app_admission/gen_v4_vectors.py --key "$(V3_SIGNING_KEY)" --foreign-key "$(V3_FOREIGN_SIGNING_KEY)" --output $@

build/%.o: $(MBEDTLS)/library/%.c | build
	$(CC) $(STRICT_CFLAGS) -w -c $< -o $@

clean:
	rm -rf build test_app_host_session *.o
