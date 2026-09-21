CC ?= cc
ROOT = ../..
CFLAGS = -Wall -Wextra -Werror -std=c11
INC = -I$(ROOT)/Custom/Hal -I$(ROOT)/Custom/Common/Lib/cJSON -I$(ROOT)/Custom/Common/Lib/pp -I$(ROOT)/Custom/Common/Inc -I$(ROOT)/Custom/Core/Video -I$(ROOT)/Custom/Tasks/Inc

META_SRC = $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_nn_model_meta.c
ACTIVE_SRC = $(ROOT)/Custom/Core/Video/video_ai_active_model.c $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_video_ai_active_model.c
RESULT_SRC = $(ROOT)/Custom/Core/Video/video_ai_result_list.c test_video_ai_result_list.c
LC_SRC = $(ROOT)/Custom/Tasks/Src/lc_tracker.c $(ROOT)/Custom/Tasks/Src/lc_line_cross.c test_lc_engine.c

test_nn_model_meta: $(META_SRC)
	$(CC) $(CFLAGS) $(INC) $(META_SRC) -o $@

test_video_ai_active_model: $(ACTIVE_SRC)
	$(CC) $(CFLAGS) $(INC) $(ACTIVE_SRC) -o $@

test_video_ai_result_list: $(RESULT_SRC)
	$(CC) $(CFLAGS) $(INC) $(RESULT_SRC) -o $@

test_lc_engine: $(LC_SRC)
	$(CC) $(CFLAGS) -D__LC_TEST__ $(INC) $(LC_SRC) -lm -o $@

.PHONY: test clean

test: test_nn_model_meta test_video_ai_active_model test_video_ai_result_list test_lc_engine
	./test_nn_model_meta
	./test_video_ai_active_model
	./test_video_ai_result_list
	./test_lc_engine

clean:
	rm -f test_nn_model_meta test_video_ai_active_model test_video_ai_result_list test_lc_engine
