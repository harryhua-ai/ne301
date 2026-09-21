CC ?= cc
ROOT = ../..
CFLAGS = -Wall -Wextra -Werror -std=c11
INC = -I$(ROOT)/Custom/Hal -I$(ROOT)/Custom/Common/Lib/cJSON

SRC = $(ROOT)/Custom/Hal/nn_model_meta.c $(ROOT)/Custom/Common/Lib/cJSON/cJSON.c test_nn_model_meta.c

test_nn_model_meta: $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

.PHONY: test clean

test: test_nn_model_meta
	./test_nn_model_meta

clean:
	rm -f test_nn_model_meta
