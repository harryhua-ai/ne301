#include "cfg_blob_store.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define PAYLOAD 333u

typedef struct {
    uint8_t buf[2u * (24u + PAYLOAD)];
    int writes;
    int fail_write_at;
    int torn_write_at;
    uint32_t torn_len;
    int fail_read_at;
    int reads;
} blob_io_t;

static aicam_result_t bio_read(void *user, uint32_t off, void *out, uint32_t len) {
    blob_io_t *b = (blob_io_t *)user;
    b->reads++;
    if (b->fail_read_at > 0 && b->reads >= b->fail_read_at) return AICAM_ERROR_IO;
    if (off + len > sizeof(b->buf)) return AICAM_ERROR_INVALID_PARAM;
    memcpy(out, b->buf + off, len);
    return AICAM_OK;
}

static aicam_result_t bio_write(void *user, uint32_t off, const void *in, uint32_t len) {
    blob_io_t *b = (blob_io_t *)user;
    b->writes++;
    if (b->fail_write_at > 0 && b->writes == b->fail_write_at) {
        b->fail_write_at = 0;
        return AICAM_ERROR_IO;
    }
    if (b->torn_write_at > 0 && b->writes == b->torn_write_at) {
        b->torn_write_at = 0;
        uint32_t n = b->torn_len < len ? b->torn_len : len;
        memcpy(b->buf + off, in, n);
        return AICAM_OK;
    }
    if (off + len > sizeof(b->buf)) return AICAM_ERROR_INVALID_PARAM;
    memcpy(b->buf + off, in, len);
    return AICAM_OK;
}

static void bio_init(blob_io_t *b) {
    memset(b->buf, 0xFF, sizeof(b->buf));
    b->writes = 0;
    b->fail_write_at = 0;
    b->torn_write_at = 0;
    b->torn_len = 0;
    b->fail_read_at = 0;
    b->reads = 0;
}

static void fill_pattern(void *dst, uint32_t seed) {
    uint8_t *p = (uint8_t *)dst;
    for (uint32_t i = 0; i < PAYLOAD; i++) p[i] = (uint8_t)(seed + i);
}

static aicam_bool_t pattern_matches(const void *got, uint32_t seed) {
    const uint8_t *p = (const uint8_t *)got;
    for (uint32_t i = 0; i < PAYLOAD; i++) {
        if (p[i] != (uint8_t)(seed + i)) return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void test_fresh_load_not_found(void) {
    blob_io_t b;
    bio_init(&b);
    cfg_blob_io_t io = { &b, bio_read, bio_write };
    cfg_blob_store_t s;
    cfg_blob_store_init(&s, &io, PAYLOAD);
    uint8_t out[PAYLOAD];
    CHECK(cfg_blob_store_load(&s, out) == AICAM_ERROR_NOT_FOUND);
    CHECK(cfg_blob_store_loaded(&s) == AICAM_FALSE);
}

static void test_save_load_reboot_roundtrip(void) {
    blob_io_t b;
    bio_init(&b);
    cfg_blob_io_t io = { &b, bio_read, bio_write };
    cfg_blob_store_t s;
    cfg_blob_store_init(&s, &io, PAYLOAD);

    uint8_t a[PAYLOAD];
    uint8_t c[PAYLOAD];
    fill_pattern(a, 10);
    fill_pattern(c, 200);
    CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);
    CHECK(cfg_blob_store_save(&s, c) == AICAM_OK);
    CHECK(s.generation == 2);

    cfg_blob_store_t rebooted;
    cfg_blob_store_init(&rebooted, &io, PAYLOAD);
    CHECK(cfg_blob_store_loaded(&rebooted) == AICAM_TRUE);
    uint8_t out[PAYLOAD];
    CHECK(cfg_blob_store_load(&rebooted, out) == AICAM_OK);
    CHECK(pattern_matches(out, 200));
    CHECK(rebooted.generation == 2);

    for (uint32_t k = 0; k < 6; k++) {
        fill_pattern(a, 50 + k);
        CHECK(cfg_blob_store_save(&rebooted, a) == AICAM_OK);
        cfg_blob_store_t again;
        cfg_blob_store_init(&again, &io, PAYLOAD);
        CHECK(cfg_blob_store_load(&again, out) == AICAM_OK);
        CHECK(pattern_matches(out, 50 + k));
    }
}

static void test_torn_write_at_every_prefix(void) {
    for (uint32_t torn = 0; torn <= 24u + PAYLOAD; torn += 7u) {
        blob_io_t b;
        bio_init(&b);
        cfg_blob_io_t io = { &b, bio_read, bio_write };
        cfg_blob_store_t s;
        cfg_blob_store_init(&s, &io, PAYLOAD);

        uint8_t a[PAYLOAD];
        uint8_t c[PAYLOAD];
        fill_pattern(a, 10);
        fill_pattern(c, 200);
        CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);

        b.torn_write_at = b.writes + 2;
        b.torn_len = torn;
        CHECK(cfg_blob_store_save(&s, c) == AICAM_OK);

        cfg_blob_store_t rebooted;
        cfg_blob_store_init(&rebooted, &io, PAYLOAD);
        uint8_t out[PAYLOAD];
        aicam_result_t r = cfg_blob_store_load(&rebooted, out);
        if (torn >= 16u) {
            CHECK(r == AICAM_OK);
            CHECK(pattern_matches(out, 200));
        } else {
            CHECK(r == AICAM_OK);
            CHECK(pattern_matches(out, 10));
        }
    }
}

static void test_torn_payload_write_keeps_old(void) {
    for (uint32_t torn = 0; torn < PAYLOAD; torn += 111u) {
        blob_io_t b;
        bio_init(&b);
        cfg_blob_io_t io = { &b, bio_read, bio_write };
        cfg_blob_store_t s;
        cfg_blob_store_init(&s, &io, PAYLOAD);

        uint8_t a[PAYLOAD];
        uint8_t c[PAYLOAD];
        fill_pattern(a, 10);
        fill_pattern(c, 200);
        CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);

        b.torn_write_at = b.writes + 1;
        b.torn_len = torn;
        CHECK(cfg_blob_store_save(&s, c) == AICAM_OK);

        cfg_blob_store_t rebooted;
        cfg_blob_store_init(&rebooted, &io, PAYLOAD);
        uint8_t out[PAYLOAD];
        aicam_result_t r = cfg_blob_store_load(&rebooted, out);
        CHECK(r == AICAM_OK);
        CHECK(pattern_matches(out, 10));
    }
}

static void test_write_failure_keeps_previous_and_retries(void) {
    blob_io_t b;
    bio_init(&b);
    cfg_blob_io_t io = { &b, bio_read, bio_write };
    cfg_blob_store_t s;
    cfg_blob_store_init(&s, &io, PAYLOAD);

    uint8_t a[PAYLOAD];
    uint8_t c[PAYLOAD];
    fill_pattern(a, 1);
    fill_pattern(c, 2);
    CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);

    b.fail_write_at = b.writes + 1;
    CHECK(cfg_blob_store_save(&s, c) == AICAM_ERROR_IO);

    cfg_blob_store_t rebooted;
    cfg_blob_store_init(&rebooted, &io, PAYLOAD);
    uint8_t out[PAYLOAD];
    CHECK(cfg_blob_store_load(&rebooted, out) == AICAM_OK);
    CHECK(pattern_matches(out, 1));

    CHECK(cfg_blob_store_save(&rebooted, c) == AICAM_OK);
    CHECK(cfg_blob_store_load(&rebooted, out) == AICAM_OK);
    CHECK(pattern_matches(out, 2));
}

static void test_read_failure_at_each_chunk_is_reported(void) {
    for (int boundary = 1; boundary <= 3; boundary++) {
        blob_io_t b;
        bio_init(&b);
        cfg_blob_io_t io = { &b, bio_read, bio_write };
        cfg_blob_store_t s;
        cfg_blob_store_init(&s, &io, PAYLOAD);
        uint8_t a[PAYLOAD];
        fill_pattern(a, 42);
        CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);

        b.reads = 0;
        b.fail_read_at = boundary;
        uint8_t out[PAYLOAD];
        CHECK(cfg_blob_store_load(&s, out) == AICAM_ERROR_IO);
    }
}

static void test_both_slots_corrupt_is_not_found(void) {
    blob_io_t b;
    bio_init(&b);
    cfg_blob_io_t io = { &b, bio_read, bio_write };
    cfg_blob_store_t s;
    cfg_blob_store_init(&s, &io, PAYLOAD);
    uint8_t a[PAYLOAD];
    fill_pattern(a, 7);
    CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);
    CHECK(cfg_blob_store_save(&s, a) == AICAM_OK);

    memset(b.buf, 0x5C, sizeof(b.buf));
    cfg_blob_store_t rebooted;
    cfg_blob_store_init(&rebooted, &io, PAYLOAD);
    uint8_t out[PAYLOAD];
    CHECK(cfg_blob_store_load(&rebooted, out) == AICAM_ERROR_NOT_FOUND);
}

int main(void) {
    test_fresh_load_not_found();
    test_save_load_reboot_roundtrip();
    test_torn_write_at_every_prefix();
    test_torn_payload_write_keeps_old();
    test_write_failure_keeps_previous_and_retries();
    test_read_failure_at_each_chunk_is_reported();
    test_both_slots_corrupt_is_not_found();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg blob store tests passed\n");
    return 0;
}
