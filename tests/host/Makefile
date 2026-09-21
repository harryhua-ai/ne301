CC ?= cc
ROOT = ../..
CFLAGS = -Wall -Wextra -Werror -std=c11
INC = -I$(ROOT)/Custom/Hal -I$(ROOT)/Custom/Common/Lib/cJSON -I$(ROOT)/Custom/Common/Lib/pp -I$(ROOT)/Custom/Common/Inc -I$(ROOT)/Custom/Core/Video -I$(ROOT)/Custom/Tasks/Inc -I$(ROOT)/Custom/Core/System

META_SRC = $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_nn_model_meta.c
ACTIVE_SRC = $(ROOT)/Custom/Core/Video/video_ai_active_model.c $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_video_ai_active_model.c
RESULT_SRC = $(ROOT)/Custom/Core/Video/video_ai_result_list.c test_video_ai_result_list.c
LC_SRC = $(ROOT)/Custom/Tasks/Src/lc_tracker.c $(ROOT)/Custom/Tasks/Src/lc_line_cross.c test_lc_engine.c
CFG_SRC = $(ROOT)/Custom/Core/System/line_counting_config.c test_line_counting_config.c
TXN_SRC = $(ROOT)/Custom/Core/System/cfg_txn.c test_cfg_txn.c
BLOB_SRC = $(ROOT)/Custom/Core/System/cfg_blob_store.c test_cfg_blob_store.c
CACHE_SRC = $(ROOT)/Custom/Core/System/cfg_blob_store.c $(ROOT)/Custom/Core/System/cfg_config_cache.c test_cfg_config_cache.c
GATE_SRC = $(ROOT)/Custom/Core/System/cfg_txn.c $(ROOT)/Custom/Core/System/cfg_writer_gate.c test_cfg_writer_gate.c
APP_SRC = $(ROOT)/Custom/Tasks/Src/line_counting.c $(ROOT)/Custom/Tasks/Src/lc_tracker.c $(ROOT)/Custom/Tasks/Src/lc_line_cross.c $(ROOT)/Custom/Core/System/line_counting_config.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_line_counting_app.c
DQ_SRC = $(ROOT)/Custom/Tasks/Src/lc_delivery_queue.c test_lc_delivery_queue.c
REPORT_SRC = $(ROOT)/Custom/Tasks/Src/line_counting.c $(ROOT)/Custom/Tasks/Src/lc_tracker.c $(ROOT)/Custom/Tasks/Src/lc_line_cross.c $(ROOT)/Custom/Core/System/line_counting_config.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_lc_report.c

test_nn_model_meta: $(META_SRC)
	$(CC) $(CFLAGS) $(INC) $(META_SRC) -o $@

test_video_ai_active_model: $(ACTIVE_SRC)
	$(CC) $(CFLAGS) $(INC) $(ACTIVE_SRC) -o $@

test_video_ai_result_list: $(RESULT_SRC)
	$(CC) $(CFLAGS) $(INC) $(RESULT_SRC) -o $@

test_lc_engine: $(LC_SRC)
	$(CC) $(CFLAGS) -D__LC_TEST__ $(INC) $(LC_SRC) -lm -o $@

test_line_counting_config: $(CFG_SRC)
	$(CC) $(CFLAGS) $(INC) $(CFG_SRC) -o $@

test_line_counting_app: $(APP_SRC)
	$(CC) $(CFLAGS) -D__LC_TEST__ $(INC) $(APP_SRC) -lm -o $@

test_lc_delivery_queue: $(DQ_SRC)
	$(CC) $(CFLAGS) $(INC) $(DQ_SRC) -o $@

test_lc_report: $(REPORT_SRC)
	$(CC) $(CFLAGS) -D__LC_TEST__ $(INC) $(REPORT_SRC) -lm -o $@

test_cfg_txn: $(TXN_SRC)
	$(CC) $(CFLAGS) $(INC) $(TXN_SRC) -o $@ -pthread

test_cfg_blob_store: $(BLOB_SRC)
	$(CC) $(CFLAGS) $(INC) $(BLOB_SRC) -o $@
test_cfg_config_cache: $(CACHE_SRC)
	$(CC) $(CFLAGS) $(INC) $(CACHE_SRC) -o $@

test_cfg_writer_gate: $(GATE_SRC)
	$(CC) $(CFLAGS) $(INC) $(GATE_SRC) -o $@ -pthread



.PHONY: test clean

test: test_nn_model_meta test_video_ai_active_model test_video_ai_result_list test_lc_engine test_line_counting_config test_line_counting_app test_cfg_txn test_cfg_blob_store test_cfg_config_cache test_cfg_writer_gate test_lc_delivery_queue test_lc_report
	./test_nn_model_meta
	./test_cfg_txn
	./test_cfg_blob_store
	./test_cfg_config_cache
	./test_cfg_writer_gate
	./test_video_ai_active_model
	./test_video_ai_result_list
	./test_lc_engine
	./test_line_counting_config
	./test_line_counting_app
	./test_lc_delivery_queue
	./test_lc_report

clean:
	rm -f test_nn_model_meta test_video_ai_active_model test_video_ai_result_list test_lc_engine test_line_counting_config test_line_counting_app test_cfg_txn test_cfg_blob_store test_cfg_config_cache test_cfg_writer_gate test_lc_delivery_queue test_lc_report
