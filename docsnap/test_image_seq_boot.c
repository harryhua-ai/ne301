/* T10d: fresh boot + storage FAULT (not -ENOENT) must refuse to emit an id
 * (A P0: unknown read state must not risk overwriting a historical counter).
 * Separate process: the boot cache starts unloaded, as on a real reboot. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "mqtt_image_seq.h"

int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,#c); failures++; } }while(0)

static int  fake_read_ret;
static char fake_read_val[12];
static int  fake_write_ok; static uint32_t fake_stored;
int storage_nvs_read(int type, const char *key, void *data, size_t len){
    (void)type;(void)key;
    if (fake_read_ret < 0) return fake_read_ret;
    snprintf((char*)data, len, "%s", fake_read_val);
    return (int)strlen(fake_read_val);
}
aicam_result_t json_config_nvs_write_uint32(const char *k, uint32_t v){
    (void)k; if(!fake_write_ok) return AICAM_ERROR; fake_stored = v; return AICAM_OK;
}

int main(void)
{
    uint32_t seq = 99;   /* sentinel: must stay untouched on refusal */
    printf("T10d fresh boot + read FAULT (-EACCES-like) refuses id\n");
    fake_read_ret = -EACCES; fake_write_ok = 1;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_ERROR);
    CHECK(seq == 99);                       /* no id emitted */

    printf("T10e fresh boot + read FAULT, write would succeed - still refuses\n");
    fake_read_ret = -EIO;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_ERROR);
    CHECK(fake_stored == 0);                /* nothing was written */
    printf(failures ? "  RESULT: FAIL (%d)\n" : "  RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
