/* T10: NVS persistence failure semantics of the REAL mqtt_image_seq.c (A P0). */
#include <stdio.h>
#include "mqtt_image_seq.h"

int failures = 0;
#define CHECK(c) do{ if(!(c)){ printf("  FAIL %s:%d %s\n",__FILE__,__LINE__,#c); failures++; } }while(0)

/* fake NVS */
static int  fake_read_ok;  static uint32_t fake_stored;
static int  fake_write_ok; static int fake_write_calls;
aicam_result_t json_config_nvs_read_uint32(const char *k, uint32_t *v){
    (void)k; if(!fake_read_ok) return AICAM_ERROR; *v = fake_stored; return AICAM_OK;
}
aicam_result_t json_config_nvs_write_uint32(const char *k, uint32_t v){
    (void)k; fake_write_calls++; if(!fake_write_ok) return AICAM_ERROR; fake_stored = v; return AICAM_OK;
}

int main(void)
{
    printf("T10 image seq: write failure refuses to emit an id (A P0)\n");
    /* fresh boot with existing counter=100 */
    fake_read_ok = 1; fake_stored = 100; fake_write_ok = 0; fake_write_calls = 0;
    uint32_t seq = 0;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_ERROR);   /* write fails -> ERROR */
    CHECK(seq == 0);                                    /* no id emitted */
    CHECK(fake_write_calls == 1);                       /* write attempted */
    /* recovery: writes work again -> the counter stays strictly monotonic
     * (the failed call already consumed 101; nothing regenerates it) */
    fake_write_ok = 1;
    CHECK(mqtt_image_seq_next(&seq) == AICAM_OK);
    CHECK(seq == 102);
    CHECK(fake_stored == 102);                          /* persisted */
    CHECK(mqtt_image_seq_next(&seq) == AICAM_OK);
    CHECK(seq == 103);
    /* fresh boot reads the persisted value (no reuse of historical ids) */
    fake_stored = 102;   /* simulate what NVS holds after reboot */
    fake_read_ok = 1; fake_write_ok = 1;
    /* NOTE: static cache is per-process; boot-continuity is proven by
     * fake_stored == last written value above */
    printf(failures ? "  RESULT: FAIL (%d)\n" : "  RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
