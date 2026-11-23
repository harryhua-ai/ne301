/* T10: NVS read-failure vs first-use vs write-failure semantics of the REAL
 * mqtt_image_seq.c (A P0). */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "mqtt_image_seq.h"

int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,#c); failures++; } }while(0)

/* fake Hal NVS */
static int  fake_read_ret;   /* bytes-read positive, or negative error */
static char fake_read_val[12];
static int  fake_write_ok; static uint32_t fake_stored; static int fake_write_calls;
int storage_nvs_read(int type, const char *key, void *data, size_t len){
    (void)type;(void)key;
    if (fake_read_ret < 0) return fake_read_ret;
    snprintf((char*)data, len, "%s", fake_read_val);
    return (int)strlen(fake_read_val);
}
aicam_result_t json_config_nvs_write_uint32(const char *k, uint32_t v){
    (void)k; fake_write_calls++; if(!fake_write_ok) return AICAM_ERROR; fake_stored = v; return AICAM_OK;
}

/* NOTE: the boot cache is static inside mqtt_image_seq.c; each scenario
 * below therefore runs in boot order, matching one device lifetime. */
int main(void)
{
    uint32_t seq = 0;
    printf("T10a first-ever use (-ENOENT) bootstraps at 1\n");
    fake_read_ret = -ENOENT; fake_write_ok = 1;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_OK);
    CHECK(seq == 1);
    CHECK(fake_stored == 1);

    printf("T10b normal continuation from persisted value\n");
    fake_read_ret = 4; snprintf(fake_read_val, sizeof(fake_read_val), "%u", 1);
    /* one process = one boot; continue from the in-boot counter (101... use
     * whatever the cache holds: after T10a it is 1 -> next is 2) */
    CHECK(mqtt_image_seq_next(&seq) == AICAM_OK);
    CHECK(seq == 2);

    printf("T10c write failure refuses to emit (A P0, kept)\n");
    fake_write_ok = 0;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_ERROR);
    CHECK(fake_write_calls >= 1);
    fake_write_ok = 1;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_OK);
    CHECK(seq > 2);   /* strictly monotonic across the failure */

    /* T10d read FAULT on a fresh boot is covered by a separate process run
     * (see test_image_seq_boot.c) - the boot cache cannot be reset in-process. */
    printf(failures ? "  RESULT: FAIL (%d)\n" : "  RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
