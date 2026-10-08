#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"

#include "app_host.h"
#include "app_host_abi.h"
#include "app_host_validate.h"
#include "storage.h"
#include "debug.h"

extern uint8_t __app_host_ram_base__[];
extern uint8_t __app_host_ram_end__[];

static int app_host_api_log(const char *text)
{
    LOG_SIMPLE("[APP] %s\r\n", (text != NULL) ? text : "");
    return 0;
}

static int app_host_api_tick_ms(void)
{
    return (int)osKernelGetTickCount();
}

static void app_host_scrub(void)
{
    int32_t size = (int32_t)(__app_host_ram_end__ - __app_host_ram_base__);
    memset(__app_host_ram_base__, 0, (size_t)size);
    SCB_CleanInvalidateDCache_by_Addr(__app_host_ram_base__, size);
}

static int app_host_read_file(const char *path, uint8_t *dst, uint32_t dst_size, uint32_t *out_len)
{
    struct stat st;
    if (flash_lfs_stat(path, &st) != 0) {
        LOG_SIMPLE("apphost: stat %s failed\r\n", path);
        return -1;
    }
    if (st.st_size <= (off_t)APP_HOST_IMAGE_HEADER_SIZE) {
        LOG_SIMPLE("apphost: %s too small (%u)\r\n", path, (unsigned int)st.st_size);
        return -2;
    }
    if ((uint32_t)st.st_size > dst_size) {
        LOG_SIMPLE("apphost: %s %u exceeds exec region %u\r\n", path, (unsigned int)st.st_size, dst_size);
        return -3;
    }

    void *fd = flash_lfs_fopen(path, "r");
    if (fd == NULL) {
        LOG_SIMPLE("apphost: open %s failed\r\n", path);
        return -4;
    }

    uint32_t total = 0;
    while (total < (uint32_t)st.st_size) {
        int n = flash_lfs_fread(fd, dst + total, (size_t)((uint32_t)st.st_size - total));
        if (n <= 0) {
            break;
        }
        total += (uint32_t)n;
    }
    flash_lfs_fclose(fd);

    if (total != (uint32_t)st.st_size) {
        LOG_SIMPLE("apphost: short read %u/%u\r\n", total, (unsigned int)st.st_size);
        return -5;
    }

    *out_len = total;
    return 0;
}

int app_host_load_and_run(const char *path)
{
    uint32_t region_size = (uint32_t)(__app_host_ram_end__ - __app_host_ram_base__);

    if (path == NULL || path[0] == '\0') {
        path = APP_HOST_DEFAULT_IMAGE_PATH;
    }

    if (!storage_is_lfs_mounted()) {
        LOG_SIMPLE("apphost: littlefs not mounted\r\n");
        return -1;
    }

    app_host_scrub();

    uint32_t file_len = 0;
    int rc = app_host_read_file(path, __app_host_ram_base__, region_size, &file_len);
    if (rc != 0) {
        app_host_scrub();
        return rc - 10;
    }

    app_host_load_policy_t policy;
    policy.expected_target_addr = (uint32_t)(uintptr_t)__app_host_ram_base__;
    policy.region_size = region_size;
    policy.abi_version = APP_HOST_ABI_VERSION;

    app_host_image_info_t info;
    app_host_check_t check = app_host_validate((const uint8_t *)(uintptr_t)__app_host_ram_base__, file_len, &policy, &info);
    if (check != APP_HOST_CHECK_OK) {
        LOG_SIMPLE("apphost: reject %s (%s)\r\n", path, app_host_check_str(check));
        app_host_scrub();
        return -20;
    }

    uint32_t header_size = ((const app_host_image_header_t *)(uintptr_t)__app_host_ram_base__)->header_size;
    memmove(__app_host_ram_base__, __app_host_ram_base__ + header_size, info.image_size);
    memset(__app_host_ram_base__ + info.image_size, 0, region_size - info.image_size);

    __DSB();
    SCB_CleanInvalidateDCache_by_Addr(__app_host_ram_base__, (int32_t)region_size);
    SCB_InvalidateICache_by_Addr(__app_host_ram_base__, (int32_t)region_size);
    __ISB();

    app_host_api_table_t api;
    api.table_size = (uint32_t)sizeof(api);
    api.abi_version = APP_HOST_ABI_VERSION;
    api.log = app_host_api_log;
    api.tick_ms = app_host_api_tick_ms;

    uintptr_t entry_addr = (uintptr_t)__app_host_ram_base__ + info.entry_offset;
    app_host_entry_fn_t entry = (app_host_entry_fn_t)(entry_addr | APP_HOST_ENTRY_THUMB_MASK);

    LOG_SIMPLE("apphost: run %s entry=0x%08x size=%u abi=0x%04x.%04x\r\n", path,
               (unsigned int)(entry_addr & 0xFFFFFFFEu), info.image_size,
               APP_HOST_ABI_VERSION_MAJOR(api.abi_version), APP_HOST_ABI_VERSION_MINOR(api.abi_version));

    int result = entry(&api, APP_HOST_ABI_VERSION);

    LOG_SIMPLE("apphost: entry returned %d\r\n", result);

    app_host_scrub();
    return (result == 0) ? 0 : 1;
}

static int apphost_cmd(int argc, char *argv[])
{
    uint32_t region_size = (uint32_t)(__app_host_ram_end__ - __app_host_ram_base__);

    if (argc >= 2 && strcmp(argv[1], "load") == 0) {
        const char *path = (argc >= 3) ? argv[2] : NULL;
        int rc = app_host_load_and_run(path);
        return (rc < 0) ? 1 : rc;
    }

    if (argc >= 2 && strcmp(argv[1], "info") == 0) {
        LOG_SIMPLE("apphost: exec region 0x%08x-0x%08x (%u bytes)\r\n",
                   (unsigned int)(uintptr_t)__app_host_ram_base__,
                   (unsigned int)(uintptr_t)__app_host_ram_end__, region_size);
        LOG_SIMPLE("apphost: abi 0x%04x.%04x default image %s\r\n",
                   APP_HOST_ABI_VERSION_MAJOR(APP_HOST_ABI_VERSION), APP_HOST_ABI_VERSION_MINOR(APP_HOST_ABI_VERSION),
                   APP_HOST_DEFAULT_IMAGE_PATH);
        LOG_SIMPLE("apphost: littlefs %s\r\n", storage_is_lfs_mounted() ? "mounted" : "not mounted");
        return 0;
    }

    LOG_SIMPLE("usage: apphost info | apphost load [path]\r\n");
    return 1;
}

static const debug_cmd_reg_t g_apphost_cmd_table[] = {
    { "apphost", "apphost info | apphost load [path]", apphost_cmd }
};

void app_host_cmd_register(void)
{
    debug_register_commands(g_apphost_cmd_table, 1u);
}
