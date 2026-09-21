CC ?= cc
ROOT = ../..
CFLAGS = -Wall -Wextra -Werror -std=c11
INC = -I$(ROOT)/Custom/Hal -I$(ROOT)/Custom/Common/Lib/cJSON -I$(ROOT)/Custom/Common/Lib/pp -I$(ROOT)/Custom/Common/Inc -I$(ROOT)/Custom/Core/Video

META_SRC = $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_nn_model_meta.c
ACTIVE_SRC = $(ROOT)/Custom/Core/Video/video_ai_active_model.c $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_video_ai_active_model.c

test_nn_model_meta: $(META_SRC)
	$(CC) $(CFLAGS) $(INC) $(META_SRC) -o $@

test_video_ai_active_model: $(ACTIVE_SRC)
	$(CC) $(CFLAGS) $(INC) $(ACTIVE_SRC) -o $@

.PHONY: test clean

test: test_nn_model_meta test_video_ai_active_model
	./test_nn_model_meta
	./test_video_ai_active_model

clean:
	rm -f test_nn_model_meta test_video_ai_active_model
