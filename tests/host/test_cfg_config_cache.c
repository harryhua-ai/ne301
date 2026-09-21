#include "cfg_config_cache.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t authoritative_generation;
    uint32_t marker_val;
} test_view_t;

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

typedef struct {
    uint8_t slot[2][CFG_BLOB_HDR_SIZE + sizeof(test_view_t)];
    uint8_t slot_written[2];
    int fail_slot_write_at;
    int slot_writes;
    cfg_cache_marker_t marker;
    int marker_present;
    int marker_corrupt;
    int fail_marker_write_at;
    int marker_writes;
} test_io_t;

static aicam_result_t tio_read_slot(void *ctx, uint32_t slot, void *buf, uint32_t len) {
    test_io_t *t = (test_io_t *)ctx;
    if (slot > 1u || !t->slot_written[slot]) return AICAM_ERROR_NOT_FOUND;
    memcpy(buf, t->slot[slot], len);
    return AICAM_OK;
}

static aicam_result_t tio_write_slot(void *ctx, uint32_t slot, const void *buf, uint32_t len) {
    test_io_t *t = (test_io_t *)ctx;
    t->slot_writes++;
    if (t->fail_slot_write_at > 0 && t->slot_writes == t->fail_slot_write_at) {
        t->fail_slot_write_at = 0;
        return AICAM_ERROR_IO;
    }
    if (slot > 1u) return AICAM_ERROR_INVALID_PARAM;
    memcpy(t->slot[slot], buf, len);
    t->slot_written[slot] = 1;
    return AICAM_OK;
}

static aicam_result_t tio_marker_read(void *ctx, cfg_cache_marker_t *m) {
    test_io_t *t = (test_io_t *)ctx;
    if (!t->marker_present) return AICAM_ERROR_NOT_FOUND;
    memcpy(m, &t->marker, sizeof(*m));
    if (t->marker_corrupt) m->crc ^= 0xFFFFFFFFu;
    return AICAM_OK;
}

static aicam_result_t tio_marker_write(void *ctx, const cfg_cache_marker_t *m) {
    test_io_t *t = (test_io_t *)ctx;
    t->marker_writes++;
    if (t->fail_marker_write_at > 0 && t->marker_writes == t->fail_marker_write_at) {
        t->fail_marker_write_at = 0;
        return AICAM_ERROR_IO;
    }
    memcpy(&t->marker, m, sizeof(*m));
    t->marker_present = 1;
    t->marker_corrupt = 0;
    return AICAM_OK;
}

static void tio_init(test_io_t *t) {
    memset(t, 0, sizeof(*t));
}

static void core_init(cfg_config_cache_core_t *c, test_io_t *t) {
    cfg_cache_io_t io = { t, tio_read_slot, tio_write_slot, tio_marker_read, tio_marker_write };
    cfg_config_cache_core_init(c, &io, (uint32_t)sizeof(test_view_t));
}

static void fill_view(test_view_t *v, uint32_t auth_gen, uint32_t marker_val) {
    memset(v, 0, sizeof(*v));
    v->authoritative_generation = auth_gen;
    v->marker_val = marker_val;
}

static aicam_bool_t view_says(const test_view_t *v, uint32_t marker_val) {
    return v->marker_val == marker_val;
}

static void commit_blob_of_gen_expect(test_io_t *t, uint32_t blob_gen, uint32_t marker_val,
                                      aicam_result_t expect) {
    cfg_config_cache_core_t c;
    core_init(&c, t);
    test_view_t v;
    fill_view(&v, blob_gen, marker_val);
    c.store.generation = blob_gen - 1u;
    c.store.has_record = 1;
    CHECK(cfg_config_cache_store(&c, &v) == expect);
    if (expect == AICAM_OK) CHECK(c.store.generation == blob_gen);
}

static void commit_blob_of_gen(test_io_t *t, uint32_t blob_gen, uint32_t marker_val) {
    commit_blob_of_gen_expect(t, blob_gen, marker_val, AICAM_OK);
}

static void test_matrix1_all_g_uses_g(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 10u, 1000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 10u) == AICAM_OK);

    test_view_t out;
    memset(&out, 0, sizeof(out));
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 1000u));
    CHECK(out.authoritative_generation == 10u);
}

static void test_matrix2_blob_ahead_cache_fail_uses_old_g(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 11u, 1100u);
    t.fail_slot_write_at = t.slot_writes + 1;
    commit_blob_of_gen_expect(&t, 12u, 1200u, AICAM_ERROR_IO);

    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 11u) == AICAM_OK);

    t.fail_slot_write_at = 0;
    uint32_t mgen = 0;
    CHECK(cfg_config_cache_marker_load(&c, &mgen) == AICAM_OK);
    CHECK(mgen == 11u);
    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 1100u));
    CHECK(out.authoritative_generation == 11u);
}

static void test_matrix3_cache_written_marker_not_must_expose_old_g(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 20u, 2000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 20u) == AICAM_OK);

    commit_blob_of_gen(&t, 21u, 2100u);
    t.fail_marker_write_at = t.marker_writes + 1;
    CHECK(cfg_config_cache_marker_store(&c, 21u) == AICAM_ERROR_IO);

    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 2000u));
    CHECK(out.authoritative_generation == 20u);
}

static void test_matrix4_all_advance_uses_new_g(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 30u, 3000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 30u) == AICAM_OK);

    commit_blob_of_gen(&t, 31u, 3100u);
    CHECK(cfg_config_cache_marker_store(&c, 31u) == AICAM_OK);

    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 3100u));
    CHECK(out.authoritative_generation == 31u);
}

static void test_matrix5_marker_present_slots_corrupt_is_safe_defaults(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 40u, 4000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 40u) == AICAM_OK);

    t.slot_written[0] = 0;
    t.slot_written[1] = 0;

    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_SAFE_DEFAULTS);
    uint32_t mgen = 0;
    CHECK(cfg_config_cache_marker_load(&c, &mgen) == AICAM_OK);
    CHECK(mgen == 40u);
}

static void test_matrix6_no_marker_no_cache_is_pre_migration(void) {
    test_io_t t;
    tio_init(&t);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_PRE_MIGRATION);
}

static void test_matrix7_repair_records_exact_authoritative_generation(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 50u, 5000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);

    test_view_t v;
    fill_view(&v, 51u, 5100u);
    c.store.generation = 50u;
    t.fail_slot_write_at = t.slot_writes + 1;
    CHECK(cfg_config_cache_store(&c, &v) == AICAM_ERROR_IO);
    fill_view(&v, 52u, 5200u);
    c.store.generation = 51u;
    t.fail_slot_write_at = t.slot_writes + 1;
    CHECK(cfg_config_cache_store(&c, &v) == AICAM_ERROR_IO);
    fill_view(&v, 53u, 5300u);
    c.store.generation = 52u;
    CHECK(cfg_config_cache_store(&c, &v) == AICAM_OK);
    CHECK(c.store.generation == 53u);

    CHECK(cfg_config_cache_load_for_generation(&c, &v, 53u) == AICAM_TRUE);
    CHECK(view_says(&v, 5300u));
    CHECK(cfg_config_cache_load_for_generation(&c, &v, 51u) == AICAM_FALSE);
    CHECK(cfg_config_cache_load_for_generation(&c, &v, 52u) == AICAM_FALSE);
}

static void test_matrix8_wrap_policy_is_deterministic(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 0xFFFFFFFFu, 0xF000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 0xFFFFFFFFu) == AICAM_OK);

    commit_blob_of_gen(&t, 1u, 0x100u);

    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_SAFE_DEFAULTS);

    CHECK(cfg_config_cache_marker_store(&c, 1u) == AICAM_OK);
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 0x100u));
}

static void test_marker_validity_and_missing_cache(void) {
    test_io_t t;
    tio_init(&t);
    cfg_config_cache_core_t c;
    core_init(&c, &t);

    uint32_t gen = 0;
    CHECK(cfg_config_cache_marker_load(&c, &gen) == AICAM_ERROR_NOT_FOUND);

    t.marker_present = 1;
    t.marker.magic = 0xDEADu;
    CHECK(cfg_config_cache_marker_load(&c, &gen) == AICAM_ERROR_IO);

    tio_init(&t);
    core_init(&c, &t);
    commit_blob_of_gen(&t, 60u, 6000u);
    t.marker_present = 1;
    t.marker.generation = 60u;
    t.marker.crc = 0u;
    CHECK(cfg_config_cache_marker_load(&c, &gen) == AICAM_ERROR_IO);
    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_SAFE_DEFAULTS);
}

static void test_torn_slot_is_never_loaded(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 70u, 7000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);

    commit_blob_of_gen(&t, 71u, 7100u);
    CHECK(cfg_config_cache_marker_store(&c, 71u) == AICAM_OK);
    memset(t.slot[1], 0xA5u, sizeof(t.slot[1]));

    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&c, &out) == CFG_CACHE_BOOT_SAFE_DEFAULTS);
    CHECK(cfg_config_cache_load_for_generation(&c, &out, 71u) == AICAM_FALSE);
    CHECK(cfg_config_cache_load_for_generation(&c, &out, 70u) == AICAM_TRUE);
    CHECK(out.authoritative_generation == 70u);
}

int main(void) {
    test_matrix1_all_g_uses_g();
    test_matrix2_blob_ahead_cache_fail_uses_old_g();
    test_matrix3_cache_written_marker_not_must_expose_old_g();
    test_matrix4_all_advance_uses_new_g();
    test_matrix5_marker_present_slots_corrupt_is_safe_defaults();
    test_matrix6_no_marker_no_cache_is_pre_migration();
    test_matrix7_repair_records_exact_authoritative_generation();
    test_matrix8_wrap_policy_is_deterministic();
    test_marker_validity_and_missing_cache();
    test_torn_slot_is_never_loaded();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg config cache tests passed\n");
    return 0;
}
