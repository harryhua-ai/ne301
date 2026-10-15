CC ?= cc
REPO_ROOT := ../..
HOST_SRC := $(REPO_ROOT)/Custom/Services/AppHost
COMMON_INC := $(REPO_ROOT)/Custom/Common/Inc
COMMON_UTILS := $(REPO_ROOT)/Custom/Common/Utils

STRICT := -std=c11 -O2 -g -Wall -Wextra -Werror
INC := -I. -Ishim -I$(HOST_SRC) -I$(COMMON_INC) -I$(COMMON_UTILS)

.PHONY: test clean
test: test_caps_probe test_live_bounded test_live_budget
	./test_caps_probe
	./test_live_bounded
	./test_live_budget

test_caps_probe: test_caps_probe.o app_admission.o generic_math.o shim/stub_mbedtls.o
	$(CC) $(STRICT) $(INC) -o $@ $^

test_live_bounded: test_live_bounded.o lc_tracker.o lc_line_cross.o
	$(CC) $(STRICT) $(INC) -o $@ $^ -lm

test_live_budget: test_live_budget.o lc_tracker.o lc_line_cross.o
	$(CC) $(STRICT) $(INC) -o $@ $^ -lm

test_caps_probe.o: test_caps_probe.c lv_wire.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

test_live_bounded.o: test_live_bounded.c lv_wire.h lc_tracker.h lc_line_cross.h lc_types.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

test_live_budget.o: test_live_budget.c lv_wire.h lc_tracker.h lc_line_cross.h lc_types.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

app_admission.o: $(HOST_SRC)/app_admission.c $(HOST_SRC)/app_admission.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

generic_math.o: $(COMMON_UTILS)/generic_math.c $(COMMON_UTILS)/generic_math.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

shim/stub_mbedtls.o: shim/stub_mbedtls.c shim/mbedtls/sha256.h shim/mbedtls/ecp.h shim/mbedtls/ecdsa.h shim/mbedtls/asn1.h shim/mbedtls/bignum.h
	$(CC) -std=c11 -O2 -w $(INC) -c $< -o $@

lc_tracker.o: lc_tracker.c lc_tracker.h lc_line_cross.h lc_types.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

lc_line_cross.o: lc_line_cross.c lc_line_cross.h lc_tracker.h lc_types.h
	$(CC) $(STRICT) $(INC) -c $< -o $@

clean:
	rm -f test_caps_probe test_live_bounded test_live_budget test_caps_probe.o test_live_bounded.o test_live_budget.o app_admission.o generic_math.o lc_tracker.o lc_line_cross.o shim/stub_mbedtls.o
