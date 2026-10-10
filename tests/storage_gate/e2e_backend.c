#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include "fake_flash.h"
#include "nvs.h"
#include "storage_media_gate.h"
#include "storage_nvs_session.h"
#include "lfs.h"
#include "cmsis_os2.h"
#include "service_interfaces.h"
#include "web_server.h"
#include "storage.h"
#include "aicam_types.h"

#define E2E_SECTOR_SIZE 4096
#define E2E_USER_SECTORS 8
#define E2E_FACT_SECTORS 4
#define E2E_USER_OFF 0x40000
#define E2E_FACT_OFF 0x30000
#define E2E_USER_SIZE (E2E_USER_SECTORS * E2E_SECTOR_SIZE)
#define E2E_FACT_SIZE (E2E_FACT_SECTORS * E2E_SECTOR_SIZE)
#define E2E_LFS_OFF 0x80000
#define E2E_LFS_BLOCK 4096
#define E2E_LFS_BLOCKS 16

static uint8_t g_user_mem[E2E_USER_SIZE];
static uint8_t g_fact_mem[E2E_FACT_SIZE];
static uint8_t g_lfs_mem[E2E_LFS_BLOCK * E2E_LFS_BLOCKS];

fake_flash_t g_e2e_user_ff;
fake_flash_t g_e2e_fact_ff;
fake_flash_t g_e2e_lfs_ff;

static nvs_fs_t g_user_nvs;
static nvs_fs_t g_fact_nvs;
bool g_user_io_fail;
static bool g_fact_io_fail;

struct lfs_config g_e2e_lfs_cfg;
static lfs_t g_e2e_lfs;
static bool g_e2e_lfs_mounted;
bool g_e2e_lfs_prog_fail;
static bool g_e2e_lfs_erased;

int g_reload_calls;

char g_last_response_kind[16];
int g_last_response_code;
char g_last_response_message[512];
bool g_service_running;

void e2e_reset_response(void)
{
    memset(g_last_response_kind, 0, sizeof(g_last_response_kind));
    g_last_response_code = 0;
    g_last_response_message[0] = '\0';
}

static void mutex_noop(void *m)
{
    (void)m;
}


static int ffdev_read(fake_flash_t *ff, uint32_t offset, void *data, size_t len)
{
    if (!ff || !ff->mem || (size_t)offset >= ff->size || (size_t)offset + len > ff->size) {
        return -1;
    }
    ff->read_calls++;
    if (ff->fail_next_reads > 0) {
        ff->fail_next_reads--;
        return -5;
    }
    memcpy(data, ff->mem + offset, len);
    return 0;
}

static int ffdev_write(fake_flash_t *ff, uint32_t offset, void *data, size_t len)
{
    if (!ff || !ff->mem || (size_t)offset >= ff->size || (size_t)offset + len > ff->size) {
        return -1;
    }
    ff->write_calls++;
    if (ff->fail_next_writes > 0) {
        ff->fail_next_writes--;
        return -5;
    }
    for (size_t i = 0; i < len; i++) {
        if ((ff->mem[offset + i] & ((const uint8_t *)data)[i]) != ((const uint8_t *)data)[i]) {
            return -2;
        }
    }
    memcpy(ff->mem + offset, data, len);
    return 0;
}

static int ffdev_erase(fake_flash_t *ff, uint32_t offset, size_t size)
{
    if (!ff || !ff->mem || (size_t)offset >= ff->size || (size_t)offset + size > ff->size) {
        return -1;
    }
    ff->erase_calls++;
    if (ff->fail_next_erases > 0) {
        ff->fail_next_erases--;
        return -5;
    }
    memset(ff->mem + offset, 0xFF, size);
    return 0;
}

static int user_region_read(uint32_t offset, void *data, size_t len)
{
    if (g_user_io_fail) {
        return -5;
    }
    return ffdev_read(&g_e2e_user_ff, offset - E2E_USER_OFF, data, len);
}

static int user_region_write(uint32_t offset, void *data, size_t len)
{
    if (!storage_media_gate_range_writable(offset, len)) {
        printf("e2e flash write refused by media gate(offset=%u, size=%zu)\r\n", (unsigned int)offset, len);
        return -1;
    }
    return ffdev_write(&g_e2e_user_ff, offset - E2E_USER_OFF, data, len);
}

static int user_region_erase(uint32_t offset, size_t size)
{
    if (!storage_media_gate_range_writable(offset, size)) {
        printf("e2e flash erase refused by media gate(offset=%u, size=%zu)\r\n", (unsigned int)offset, size);
        return -1;
    }
    return ffdev_erase(&g_e2e_user_ff, offset - E2E_USER_OFF, size);
}

static int fact_region_read(uint32_t offset, void *data, size_t len)
{
    if (g_fact_io_fail) {
        return -5;
    }
    return ffdev_read(&g_e2e_fact_ff, offset - E2E_FACT_OFF, data, len);
}

static int fact_region_write(uint32_t offset, void *data, size_t len)
{
    if (!storage_media_gate_range_writable(offset, len)) {
        printf("e2e flash write refused by media gate(offset=%u, size=%zu)\r\n", (unsigned int)offset, len);
        return -1;
    }
    return ffdev_write(&g_e2e_fact_ff, offset - E2E_FACT_OFF, data, len);
}

static int fact_region_erase(uint32_t offset, size_t size)
{
    if (!storage_media_gate_range_writable(offset, size)) {
        printf("e2e flash erase refused by media gate(offset=%u, size=%zu)\r\n", (unsigned int)offset, size);
        return -1;
    }
    return ffdev_erase(&g_e2e_fact_ff, offset - E2E_FACT_OFF, size);
}

void e2e_storage_reset(void)
{
    ff_init(&g_e2e_user_ff, g_user_mem, sizeof(g_user_mem));
    ff_init(&g_e2e_fact_ff, g_fact_mem, sizeof(g_fact_mem));
    ff_init(&g_e2e_lfs_ff, g_lfs_mem, sizeof(g_lfs_mem));
    memset(&g_user_nvs, 0, sizeof(g_user_nvs));
    memset(&g_fact_nvs, 0, sizeof(g_fact_nvs));
    g_user_io_fail = false;
    g_fact_io_fail = false;
    g_e2e_lfs_prog_fail = false;
    g_e2e_lfs_erased = false;
    g_e2e_lfs_mounted = false;
    g_reload_calls = 0;
    g_service_running = true;
    e2e_reset_response();
    storage_media_gate_reset();
    storage_media_gate_register_region(E2E_FACT_OFF, E2E_FACT_SIZE, STORAGE_MEDIA_GATE_FACTORY);
    storage_media_gate_register_region(E2E_USER_OFF, E2E_USER_SIZE, STORAGE_MEDIA_GATE_USER);
}

int e2e_storage_boot(void)
{
    storage_nvs_session_io_t io;
    int rc;

    memset(&io, 0, sizeof(io));
    io.flash_ops.flash_read = fact_region_read;
    io.flash_ops.flash_write = fact_region_write;
    io.flash_ops.flash_erase = fact_region_erase;
    io.lock = mutex_noop;
    io.unlock = mutex_noop;
    rc = storage_nvs_session_init(&g_fact_nvs, E2E_FACT_OFF, E2E_SECTOR_SIZE, E2E_FACT_SECTORS,
                                  STORAGE_MEDIA_GATE_FACTORY, &io);
    if (rc != 0) {
        return rc;
    }

    memset(&io, 0, sizeof(io));
    io.flash_ops.flash_read = user_region_read;
    io.flash_ops.flash_write = user_region_write;
    io.flash_ops.flash_erase = user_region_erase;
    io.lock = mutex_noop;
    io.unlock = mutex_noop;
    rc = storage_nvs_session_init(&g_user_nvs, E2E_USER_OFF, E2E_SECTOR_SIZE, E2E_USER_SECTORS,
                                  STORAGE_MEDIA_GATE_USER, &io);
    return rc;
}

static nvs_fs_t *nvs_for(NVS_Type_t type)
{
    if (type == NVS_FACTORY) {
        return &g_fact_nvs;
    }
    if (type == NVS_USER) {
        return &g_user_nvs;
    }
    return NULL;
}

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data, size_t len)
{
    nvs_fs_t *nvs = nvs_for(type);
    if (!nvs) {
        return -1;
    }
    if (!storage_media_gate_writable((int)type)) {
        printf("e2e nvs write refused by media gate(part=%d, key=%s)\r\n", (int)type, key ? key : "");
        return -1;
    }
    if (nvs->ready != true) {
        return -1;
    }
    return (int)nvs_write(nvs, key, data, len);
}

int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len)
{
    nvs_fs_t *nvs = nvs_for(type);
    if (!nvs) {
        return -1;
    }
    return (int)nvs_read(nvs, key, data, len);
}

int storage_nvs_delete(NVS_Type_t type, const char *key)
{
    nvs_fs_t *nvs = nvs_for(type);
    if (!nvs) {
        return -1;
    }
    if (!storage_media_gate_writable((int)type)) {
        printf("e2e nvs delete refused by media gate(part=%d, key=%s)\r\n", (int)type, key ? key : "");
        return -1;
    }
    return nvs_delete(nvs, key);
}

int storage_nvs_clear(NVS_Type_t type)
{
    nvs_fs_t *nvs = nvs_for(type);
    if (!nvs) {
        return -1;
    }
    if (!storage_media_gate_writable((int)type)) {
        printf("e2e nvs clear refused by media gate(part=%d)\r\n", (int)type);
        return -1;
    }
    return nvs_clear(nvs);
}

storage_media_state_t storage_nvs_media_state(NVS_Type_t type)
{
    if (type != NVS_FACTORY && type != NVS_USER) {
        return STORAGE_MEDIA_UNTRUSTED;
    }
    return storage_media_gate_state((int)type);
}

int storage_flash_read(uint32_t offset, void *data, size_t size)
{
    uint8_t *src = NULL;
    if (offset >= E2E_USER_OFF && offset + size <= E2E_USER_OFF + E2E_USER_SIZE) {
        src = g_user_mem + (offset - E2E_USER_OFF);
    } else if (offset >= E2E_FACT_OFF && offset + size <= E2E_FACT_OFF + E2E_FACT_SIZE) {
        src = g_fact_mem + (offset - E2E_FACT_OFF);
    }
    if (src && data) {
        memcpy(data, src, size);
    }
    return 0;
}

static int e2e_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                        void *data, lfs_size_t size)
{
    (void)c;
    return ffdev_read(&g_e2e_lfs_ff, block * E2E_LFS_BLOCK + off, data, size);
}

static int e2e_lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                        const void *data, lfs_size_t size)
{
    (void)c;
    if (g_e2e_lfs_prog_fail) {
        return LFS_ERR_IO;
    }
    g_e2e_lfs_erased = true;
    return ffdev_write(&g_e2e_lfs_ff, block * E2E_LFS_BLOCK + off, (void *)(uintptr_t)data, size);
}

static int e2e_lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    if (g_e2e_lfs_prog_fail) {
        return LFS_ERR_IO;
    }
    g_e2e_lfs_erased = true;
    return ffdev_erase(&g_e2e_lfs_ff, block * E2E_LFS_BLOCK, E2E_LFS_BLOCK);
}

static int e2e_lfs_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

int storage_format(void)
{
    memset(&g_e2e_lfs_cfg, 0, sizeof(g_e2e_lfs_cfg));
    g_e2e_lfs_cfg.read = e2e_lfs_read;
    g_e2e_lfs_cfg.prog = e2e_lfs_prog;
    g_e2e_lfs_cfg.erase = e2e_lfs_erase;
    g_e2e_lfs_cfg.sync = e2e_lfs_sync;
    g_e2e_lfs_cfg.read_size = 256;
    g_e2e_lfs_cfg.prog_size = 256;
    g_e2e_lfs_cfg.block_size = E2E_LFS_BLOCK;
    g_e2e_lfs_cfg.block_count = E2E_LFS_BLOCKS;
    g_e2e_lfs_cfg.cache_size = 256;
    g_e2e_lfs_cfg.lookahead_size = 8;
    g_e2e_lfs_cfg.block_cycles = 0;

    int err = lfs_format(&g_e2e_lfs, &g_e2e_lfs_cfg);
    if (err == LFS_ERR_OK) {
        err = lfs_mount(&g_e2e_lfs, &g_e2e_lfs_cfg);
        g_e2e_lfs_mounted = (err == LFS_ERR_OK);
    } else {
        g_e2e_lfs_mounted = false;
    }
    return (err == LFS_ERR_OK && g_e2e_lfs_mounted) ? 0 : -1;
}

void *flash_lfs_fopen(const char *path, const char *mode)
{
    (void)path;
    (void)mode;
    return NULL;
}

int flash_lfs_fclose(void *fd) { (void)fd; return -1; }
int flash_lfs_fwrite(void *fd, const void *buf, size_t size) { (void)fd; (void)buf; (void)size; return -1; }
int flash_lfs_fread(void *fd, void *buf, size_t size) { (void)fd; (void)buf; (void)size; return -1; }
int flash_lfs_remove(const char *path) { (void)path; return -1; }
int flash_lfs_rename(const char *o, const char *n) { (void)o; (void)n; return -1; }
long flash_lfs_ftell(void *fd) { (void)fd; return -1; }
int flash_lfs_fseek(void *fd, long offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
int flash_lfs_fflush(void *fd) { (void)fd; return -1; }
void *flash_lfs_opendir(const char *path) { (void)path; return NULL; }
int flash_lfs_readdir(void *dd, char *info) { (void)dd; (void)info; return 0; }
int flash_lfs_closedir(void *dd) { (void)dd; return -1; }
int flash_lfs_stat(const char *filename, struct stat *st) { (void)filename; (void)st; return -1; }
int flash_lfs_mkdir(const char *path) { (void)path; return -1; }

osMutexId_t osMutexNew(const void *attribute)
{
    (void)attribute;
    void *m = malloc(8);
    return m;
}

int32_t osMutexDelete(osMutexId_t mutex)
{
    free(mutex);
    return 0;
}

int32_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout)
{
    (void)mutex;
    (void)timeout;
    return 0;
}

int32_t osMutexRelease(osMutexId_t mutex)
{
    (void)mutex;
    return 0;
}

void *buffer_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void buffer_free(void *ptr)
{
    free(ptr);
}

const char *board_hw_version_str(void)
{
    return "e2e-board-1";
}

uint64_t rtc_get_timeStamp(void)
{
    static uint64_t t;
    return ++t;
}

uint64_t rtc_get_local_timestamp(void)
{
    return rtc_get_timeStamp();
}

bool rtc_setup_by_timestamp(uint64_t timestamp, int timezone_offset_hours)
{
    (void)timestamp;
    (void)timezone_offset_hours;
    return true;
}

int factory_mac_get_burned(uint8_t *mac)
{
    if (!mac) {
        return -1;
    }
    mac[0] = 0x02;
    mac[1] = 0x00;
    mac[2] = 0x00;
    mac[3] = 0x00;
    mac[4] = 0x00;
    mac[5] = 0x01;
    return 0;
}

int wifi_get_flash_version(char *version, size_t size)
{
    if (version && size > 0) {
        snprintf(version, size, "1.0.0");
    }
    return 0;
}

int wifi_get_running_version(char *version, size_t size)
{
    if (version && size > 0) {
        snprintf(version, size, "1.0.0");
    }
    return 0;
}

void upload_coordinator_reload_config(void)
{
    g_reload_calls++;
}

service_state_t device_service_get_state(void)
{
    return g_service_running ? SERVICE_STATE_RUNNING : SERVICE_STATE_ERROR;
}

aicam_bool_t web_api_verify_method(http_handler_context_t *ctx, const char *method)
{
    if (!ctx || !method) {
        return AICAM_FALSE;
    }
    return (strcmp(ctx->request.method, method) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data, const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "success");
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    if (data) {
        free((void *)data);
    }
    return AICAM_OK;
}

aicam_result_t api_response_success_static(http_handler_context_t *ctx, const char *data, const char *message)
{
    (void)data;
    return api_response_success(ctx, NULL, message);
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code, const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "error");
    g_last_response_code = (int)error_code;
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    return AICAM_ERROR;
}

aicam_result_t api_response_error_data(http_handler_context_t *ctx, api_error_code_t error_code,
                                       const char *message, const char *data)
{
    (void)data;
    return api_response_error(ctx, error_code, message);
}
