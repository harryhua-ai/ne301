CC ?= cc
REPO_ROOT := ../..

INC := -I$(REPO_ROOT)/Custom/Services/AppHost \
       -I$(REPO_ROOT)/Custom/Common/Inc

STRICT_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -pthread $(INC)

CORE_OBJS := app_report_service.o

.PHONY: test clean
test: test_app_report
	./test_app_report

test_app_report: test_app_report.o $(CORE_OBJS)
	$(CC) $(STRICT_CFLAGS) -o $@ $^

test_app_report.o: test_app_report.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

app_report_service.o: $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.c $(REPO_ROOT)/Custom/Services/AppHost/app_report_service.h
	$(CC) $(STRICT_CFLAGS) -c $< -o $@

clean:
	rm -f test_app_report *.o
