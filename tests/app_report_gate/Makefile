CC ?= cc
REPO_ROOT := ../..
MBEDTLS := $(REPO_ROOT)/Custom/Common/Lib/MbedTLS

INC := -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/tests/sign_spike/shim \
       -I$(REPO_ROOT)/tests/storage_gate/shim \
       -I$(REPO_ROOT)/Custom/Hal \
       -I$(REPO_ROOT)/Custom/Core/Video \
       -I$(REPO_ROOT)/Custom/Core/System \
       -I$(REPO_ROOT)/Custom/Core/Data \
       -I$(REPO_ROOT)/Custom/Services/Video \
       -I$(REPO_ROOT)/Custom/Services/AI \
       -I$(REPO_ROOT)/Custom/Common/Lib/nvs \
       -I$(REPO_ROOT)/Custom/Common/Lib/pp \
       -I$(REPO_ROOT)/Custom/Core/Video/Pipeline \
       -I$(REPO_ROOT)/Custom/Common/Lib/cJSON \
       -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Services/Web \
       -I$(REPO_ROOT)/Custom/Services/Web/api \
       -I$(REPO_ROOT)/Custom/Common/Inc \
       -I$(REPO_ROOT)/Custom/Common/Utils \
       -I$(REPO_ROOT)/Custom/Common/Lib \
       -I$(REPO_ROOT)/Custom/Common/Lib/mongoose \
       -I$(REPO_ROOT)/Middlewares/ST/STM32_ISP_Library/isp/Inc \
       -I$(REPO_ROOT)/BSP/Utilities/Fonts \
       -I. \
       -I$(MBEDTLS)/include \
       -I$(MBEDTLS)/configs

DEFS := -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' -DMBEDTLS_ALLOW_PRIVATE_ACCESS \
        -D_DEFAULT_SOURCE -D'__packed=__attribute__((packed))'

GATE_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror $(INC) $(DEFS) -pthread
LEGACY_CFLAGS := -std=c11 -O1 -g -Wno-everything $(INC) $(DEFS)

PTHREAD_FLAGS := -pthread

.PHONY: test clean
test: test_app_report_gate
	./test_app_report_gate

test_app_report_gate: test_app_report_gate.o api_app_report_module.o app_report_service.o cJSON.o web_api.o mongoose.o gen_stubs.o
	$(CC) $(GATE_CFLAGS) $(PTHREAD_FLAGS) -o $@ test_app_report_gate.o api_app_report_module.o app_report_service.o cJSON.o web_api.o mongoose.o gen_stubs.o

test_app_report_gate.o: test_app_report_gate.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h $(REPO_ROOT)/Custom/Services/AppHost/app_host_session.h
	$(CC) $(GATE_CFLAGS) -c $< -o $@

api_app_report_module.o: $(REPO_ROOT)/Custom/Services/Web/api/api_app_report_module.c $(REPO_ROOT)/Custom/Services/Web/api/api_app_report_module.h
	$(CC) $(GATE_CFLAGS) -c $< -o $@

app_report_service.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(GATE_CFLAGS) -c $< -o $@

cJSON.o: $(REPO_ROOT)/Custom/Common/Lib/cJSON/cJSON.c
	$(CC) $(LEGACY_CFLAGS) -c $< -o $@

web_api.o: $(REPO_ROOT)/Custom/Services/Web/web_api.c
	$(CC) $(LEGACY_CFLAGS) -c $< -o $@

mongoose.o: $(REPO_ROOT)/Custom/Common/Lib/mongoose/mongoose.c
	$(CC) $(LEGACY_CFLAGS) -DMG_TLS=0 -c $< -o $@

gen_stubs.c: test_app_report_gate.o api_app_report_module.o app_report_service.o cJSON.o web_api.o mongoose.o $(REPO_ROOT)/tests/storage_gate/gen_stubs.py FORCE
	@python3 $(REPO_ROOT)/tests/storage_gate/gen_stubs.py test_app_report_gate.o api_app_report_module.o app_report_service.o cJSON.o web_api.o mongoose.o

gen_stubs.o: gen_stubs.c
	$(CC) $(GATE_CFLAGS) -w -c $< -o $@

FORCE:

clean:
	rm -rf test_app_report_gate *.o gen_stubs.c
