CC ?= cc
REPO_ROOT := ../..
APPHOST := $(REPO_ROOT)/Custom/Services/AppHost
COMMON_INC := $(REPO_ROOT)/Custom/Common/Inc

CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -I$(APPHOST) -I$(COMMON_INC)

.PHONY: test model clean
test: test_queue_capacity_layout
	./test_queue_capacity_layout

model:
	python3 capacity_model.py

test_queue_capacity_layout: test_queue_capacity_layout.c $(APPHOST)/app_report_service.c $(APPHOST)/app_report_service.h $(APPHOST)/app_report_policy.c $(APPHOST)/app_report_policy.h
	$(CC) $(CFLAGS) -o $@ test_queue_capacity_layout.c $(APPHOST)/app_report_policy.c

clean:
	rm -f test_queue_capacity_layout
