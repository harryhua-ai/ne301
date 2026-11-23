#ifndef STUB_STORAGE_H
#define STUB_STORAGE_H
#include <stdint.h>
#include <stddef.h>
#define NVS_USER 1
int storage_nvs_read(int type, const char *key, void *data, size_t len);
int storage_nvs_write(int type, const char *key, const void *data, size_t len);
#endif
