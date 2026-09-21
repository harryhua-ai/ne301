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

static void test_runtime_commit_advances_marker_immediate_reboot(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 10u, 1000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 10u) == AICAM_OK);

    commit_blob_of_gen(&t, 11u, 1100u);
    CHECK(cfg_config_cache_marker_store(&c, 11u) == AICAM_OK);

    cfg_config_cache_core_t rebooted;
    core_init(&rebooted, &t);
    test_view_t out;
    memset(&out, 0, sizeof(out));
    CHECK(cfg_config_cache_boot_source(&rebooted, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 1100u));
    CHECK(out.authoritative_generation == 11u);
}

static void test_runtime_cache_fail_keeps_old_marker_generation(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 60u, 6000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 60u) == AICAM_OK);

    t.fail_slot_write_at = t.slot_writes + 1;
    commit_blob_of_gen_expect(&t, 61u, 6100u, AICAM_ERROR_IO);

    cfg_config_cache_core_t rebooted;
    core_init(&rebooted, &t);
    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&rebooted, &out) == CFG_CACHE_BOOT_COMMITTED);
    CHECK(view_says(&out, 6000u));
    CHECK(out.authoritative_generation == 60u);
}

static void test_marker_slot_overwritten_twice_is_safe_defaults(void) {
    test_io_t t;
    tio_init(&t);
    commit_blob_of_gen(&t, 50u, 5000u);
    cfg_config_cache_core_t c;
    core_init(&c, &t);
    CHECK(cfg_config_cache_marker_store(&c, 50u) == AICAM_OK);

    commit_blob_of_gen(&t, 51u, 5100u);
    commit_blob_of_gen(&t, 52u, 5200u);

    cfg_config_cache_core_t rebooted;
    core_init(&rebooted, &t);
    test_view_t out;
    CHECK(cfg_config_cache_boot_source(&rebooted, &out) == CFG_CACHE_BOOT_SAFE_DEFAULTS);
    CHECK(cfg_config_cache_load_for_generation(&rebooted, &out, 50u) == AICAM_FALSE);
    CHECK(cfg_config_cache_load_for_generation(&rebooted, &out, 51u) == AICAM_TRUE);
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

typedef struct {
    uint8_t data[2u * (24u + sizeof(test_view_t))];
    int writes;
    int fail_write_at;
    int fail_read_at;
    int reads;
} auth_io_t;

static aicam_result_t aio_read(void *user, uint32_t off, void *out, uint32_t len) {
    auth_io_t *a = (auth_io_t *)user;
    a->reads++;
    if (a->fail_read_at > 0 && a->reads >= a->fail_read_at) return AICAM_ERROR_IO;
    if (off + len > sizeof(a->data)) return AICAM_ERROR_INVALID_PARAM;
    memcpy(out, a->data + off, len);
    return AICAM_OK;
}

static aicam_result_t aio_write(void *user, uint32_t off, const void *in, uint32_t len) {
    auth_io_t *a = (auth_io_t *)user;
    a->writes++;
    if (a->fail_write_at > 0 && a->writes == a->fail_write_at) {
        a->fail_write_at = 0;
        return AICAM_ERROR_IO;
    }
    if (off + len > sizeof(a->data)) return AICAM_ERROR_INVALID_PARAM;
    memcpy(a->data + off, in, len);
    return AICAM_OK;
}

static aicam_bool_t init_authority_attempt(auth_io_t *a, int marker_present, uint32_t *pub_count,
                                           uint32_t *published_gen) {
    cfg_blob_io_t io = { a, aio_read, aio_write };
    cfg_blob_store_t blob;
    cfg_blob_store_init(&blob, &io, sizeof(test_view_t));

    test_view_t candidate;
    memset(&candidate, 0, sizeof(candidate));

    cfg_blob_recovery_t policy = cfg_blob_store_recovery_policy(
        cfg_blob_store_loaded(&blob), marker_present ? AICAM_TRUE : AICAM_FALSE);

    aicam_result_t result = AICAM_ERROR_NOT_FOUND;
    aicam_bool_t authority_ok = AICAM_FALSE;
    if (policy == CFG_BLOB_RECOVERY_USE_AUTHORITATIVE) {
        result = cfg_blob_store_load(&blob, &candidate);
        if (result == AICAM_OK) authority_ok = AICAM_TRUE;
    } else {
        candidate.authoritative_generation = (uint32_t)(blob.generation + 1u);
        result = cfg_blob_store_save(&blob, &candidate);
        if (result == AICAM_OK) authority_ok = AICAM_TRUE;
    }

    if (!authority_ok) return AICAM_FALSE;

    *pub_count += 1;
    *published_gen = candidate.authoritative_generation;
    return AICAM_TRUE;
}

static void test_init_authority_failure_matrix(void) {
    uint32_t pub_count = 0;
    uint32_t published_gen = 0;

    auth_io_t a;
    memset(&a, 0, sizeof(a));
    cfg_blob_io_t io = { &a, aio_read, aio_write };
    cfg_blob_store_t seed;
    cfg_blob_store_init(&seed, &io, sizeof(test_view_t));
    test_view_t v;
    memset(&v, 0, sizeof(v));
    v.authoritative_generation = 1u;
    CHECK(cfg_blob_store_save(&seed, &v) == AICAM_OK);

    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_TRUE);
    CHECK(pub_count == 1);
    CHECK(published_gen == 1u);

    a.reads = 0;
    a.fail_read_at = 5;
    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_FALSE);
    CHECK(pub_count == 1);

    a.fail_read_at = 0;
    a.reads = 0;
    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_TRUE);
    CHECK(pub_count == 2);
    CHECK(published_gen == 1u);

    memset(&a, 0, sizeof(a));
    a.fail_write_at = 1;
    CHECK(init_authority_attempt(&a, 0, &pub_count, &published_gen) == AICAM_FALSE);
    CHECK(pub_count == 2);

    a.fail_write_at = 0;
    a.writes = 0;
    CHECK(init_authority_attempt(&a, 0, &pub_count, &published_gen) == AICAM_TRUE);
    CHECK(pub_count == 3);
    CHECK(published_gen >= 1u);

    memset(&a, 0, sizeof(a));
    a.fail_write_at = 1;
    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_FALSE);
    CHECK(pub_count == 3);

    a.fail_write_at = 0;
    a.writes = 0;
    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_TRUE);
    CHECK(pub_count == 4);
    CHECK(published_gen >= 1u);

    memset(&a, 0, sizeof(a));
    memset(a.data, 0x5C, sizeof(a.data));
    a.fail_write_at = 1;
    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_FALSE);
    CHECK(pub_count == 4);

    a.fail_write_at = 0;
    a.writes = 0;
    CHECK(init_authority_attempt(&a, 1, &pub_count, &published_gen) == AICAM_TRUE);
    CHECK(pub_count == 5);
}

int main(void) {
    test_matrix1_all_g_uses_g();
    test_matrix2_blob_ahead_cache_fail_uses_old_g();
    test_matrix3_cache_written_marker_not_must_expose_old_g();
    test_matrix4_all_advance_uses_new_g();
    test_matrix5_marker_present_slots_corrupt_is_safe_defaults();
    test_matrix6_no_marker_no_cache_is_pre_migration();
    test_matrix7_repair_records_exact_authoritative_generation();
    test_runtime_commit_advances_marker_immediate_reboot();
    test_runtime_cache_fail_keeps_old_marker_generation();
    test_marker_slot_overwritten_twice_is_safe_defaults();
    test_matrix8_wrap_policy_is_deterministic();
    test_marker_validity_and_missing_cache();
    test_torn_slot_is_never_loaded();
    test_init_authority_failure_matrix();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all cfg config cache tests passed\n");
    return 0;
}
