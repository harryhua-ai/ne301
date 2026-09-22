#include "lc_delivery_queue.h"
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

#define FAKE_REGION LC_DQ_REGION_SIZE
#define PHYS_JOURNAL (LC_DQ_JOURNAL_HEADER + LC_DQ_JOURNAL_ENTRY * 128u)
#define PHYS_SLOTS (LC_DQ_MAX_SLOTS * (LC_DQ_SLOT_HEADER_SIZE + LC_DQ_SLOT_CAPACITY))

typedef struct {
    uint8_t  buf[FAKE_REGION];
    int      write_calls;
    int      fail_write_at;
    uint32_t fail_write_len;
    int      read_calls;
    int      fail_read_at;
} fake_storage_t;

static fake_storage_t g_fs;

static aicam_result_t fs_read(void *user, uint32_t offset, void *buf, uint32_t len) {
    fake_storage_t *f = (fake_storage_t *)user;
    f->read_calls++;
    if (f->fail_read_at > 0 && f->read_calls >= f->fail_read_at) return AICAM_ERROR_IO;
    if (offset + len > FAKE_REGION) return AICAM_ERROR_INVALID_PARAM;
    memcpy(buf, f->buf + offset, len);
    return AICAM_OK;
}

static aicam_result_t fs_write(void *user, uint32_t offset, const void *buf, uint32_t len) {
    fake_storage_t *f = (fake_storage_t *)user;
    f->write_calls++;
    if (f->fail_write_at > 0 && f->write_calls == f->fail_write_at) {
        f->fail_write_at = 0;
        if (f->fail_write_len == 0) return AICAM_ERROR_IO;
        uint32_t n = f->fail_write_len < len ? f->fail_write_len : len;
        memcpy(f->buf + offset, buf, n);
        f->fail_write_len = 0;
        return AICAM_OK;
    }
    if (offset + len > FAKE_REGION) return AICAM_ERROR_INVALID_PARAM;
    memcpy(f->buf + offset, buf, len);
    return AICAM_OK;
}

static aicam_result_t fs_erase(void *user) {
    fake_storage_t *f = (fake_storage_t *)user;
    memset(f->buf, 0xFF, sizeof(f->buf));
    f->write_calls = 0;
    f->read_calls = 0;
    return AICAM_OK;
}

static void storage_ops(lc_dq_storage_t *ops) {
    ops->user = &g_fs;
    ops->read = fs_read;
    ops->write = fs_write;
    ops->erase_region = fs_erase;
}

static void limits_std(lc_dq_limits_t *lim) {
    lim->max_count = 16;
    lim->journal_entries = 32;
    lim->max_bytes = 64u * 1024u;
}

static aicam_result_t q_init(lc_delivery_queue_t *q) {
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    return lc_delivery_queue_init(q, &ops, &lim);
}

static uint32_t meta_payload(const lc_delivery_meta_t *m, char *buf) {
    int n = snprintf(buf, 64, "{\"report_seq\":%lu,\"in\":%lu}",
                     (unsigned long)m->report_seq, (unsigned long)m->report_seq * 2u);
    return (uint32_t)n;
}

static void enqueue_one(lc_delivery_queue_t *q, uint32_t seq) {
    lc_delivery_meta_t m = { .boot_id = 42, .report_seq = seq,
                             .mqtt = LC_DELIVERY_PENDING,
                             .webhook = LC_DELIVERY_PENDING };
    char buf[64];
    uint32_t n = meta_payload(&m, buf);
    aicam_result_t r = lc_delivery_queue_enqueue(q, &m, buf, n);
    if (r != AICAM_OK) {
        printf("FAIL enqueue seq=%lu r=%d\n", (unsigned long)seq, r);
        g_failures++;
    }
}

static void test_enqueue_peek_reopen_fifo(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 3; s++) enqueue_one(&q, s);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(st.bytes > 0);

    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(out.boot_id == 42);
    CHECK(out.mqtt == LC_DELIVERY_PENDING);
    CHECK(strstr(pbuf, "\"report_seq\":1") != NULL);
    CHECK(plen == strlen(pbuf));

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_independent_transport_states(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);

    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);
    CHECK(out.webhook == LC_DELIVERY_PENDING);

    CHECK(q_init(&q) == AICAM_OK);
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);
    CHECK(out.webhook == LC_DELIVERY_PENDING);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_count_overflow_drops_oldest(void) {
    lc_delivery_queue_t q;
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    lim.max_count = 2;
    CHECK(lc_delivery_queue_init(&q, &ops, &lim) == AICAM_OK);

    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    enqueue_one(&q, 3);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    CHECK(st.dropped_mqtt == 1);
    CHECK(st.dropped_webhook == 1);

    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 2);
    lc_delivery_queue_clear(&q);
}

static void test_byte_overflow_drops_oldest(void) {
    lc_delivery_queue_t q;
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    lim.max_bytes = 60;
    CHECK(lc_delivery_queue_init(&q, &ops, &lim) == AICAM_OK);

    for (uint32_t s = 1; s <= 5; s++) enqueue_one(&q, s);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count < 5);
    CHECK(st.dropped_mqtt == st.dropped_webhook);
    CHECK(st.dropped_mqtt > 0);

    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq > 1);
    lc_delivery_queue_clear(&q);
}

static void test_clear_reopen_empty(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 4; s++) enqueue_one(&q, s);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    CHECK(st.bytes == 0);

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_ERROR_NOT_FOUND);
}

static void test_torn_without_commit_marker(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);

    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    g_fs.write_calls = 0;
    uint32_t payload_off = LC_DQ_SUPER_SIZE + 2u * PHYS_JOURNAL
                         + LC_DQ_SLOT_HEADER_SIZE;
    uint8_t marker_zero[4] = {0, 0, 0, 0};
    CHECK(ops.write(ops.user, payload_off - 4, marker_zero, 4) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);

    enqueue_one(&q, 2);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 2);
    lc_delivery_queue_clear(&q);
}

static void test_torn_payload_discarded(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);

    g_fs.fail_write_at = g_fs.write_calls + 1;
    g_fs.fail_write_len = 10;
    g_fs.fail_write_at = g_fs.write_calls + 1;
    lc_delivery_meta_t m = { .boot_id = 1, .report_seq = 1,
                             .mqtt = LC_DELIVERY_PENDING,
                             .webhook = LC_DELIVERY_PENDING };
    char buf[64];
    uint32_t n = meta_payload(&m, buf);
    CHECK(lc_delivery_queue_enqueue(&q, &m, buf, n) == AICAM_OK);

    g_fs.fail_write_len = 0;
    g_fs.fail_write_at = 0;
    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    lc_delivery_queue_clear(&q);
}

static void test_torn_journal_entry_keeps_prior_state(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);
    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);

    uint8_t junk[LC_DQ_JOURNAL_ENTRY];
    memset(junk, 0xA5, sizeof(junk));
    uint32_t entry_off = LC_DQ_SUPER_SIZE + LC_DQ_JOURNAL_HEADER;
    uint32_t n_entries = 0;
    uint8_t probe[LC_DQ_JOURNAL_ENTRY];
    lc_dq_storage_t ops;
    storage_ops(&ops);
    while (n_entries < 128u) {
        CHECK(ops.read(ops.user, entry_off + n_entries * LC_DQ_JOURNAL_ENTRY,
                       probe, sizeof(probe)) == AICAM_OK);
        if (probe[0] == 0xA5 || probe[0] == 0xFF) break;
        n_entries++;
    }
    CHECK(ops.write(ops.user, entry_off + n_entries * LC_DQ_JOURNAL_ENTRY,
                    junk, sizeof(junk) - 4) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);
    lc_delivery_queue_clear(&q);
}

static void test_corrupt_payload_crc_discarded(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);

    uint32_t payload_off = LC_DQ_SUPER_SIZE + 2u * PHYS_JOURNAL
                         + LC_DQ_SLOT_HEADER_SIZE;
    uint8_t byte;
    lc_dq_storage_t ops;
    storage_ops(&ops);
    CHECK(ops.read(ops.user, payload_off, &byte, 1) == AICAM_OK);
    byte ^= 0xFF;
    CHECK(ops.write(ops.user, payload_off, &byte, 1) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    lc_delivery_queue_clear(&q);
}

static void test_journal_compaction_switch(void) {
    lc_delivery_queue_t q;
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    lim.journal_entries = 4;
    CHECK(lc_delivery_queue_init(&q, &ops, &lim) == AICAM_OK);

    for (uint32_t s = 1; s <= 3; s++) enqueue_one(&q, s);
    for (uint32_t s = 1; s <= 3; s++) {
        CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, s) == AICAM_OK);
        CHECK(lc_delivery_queue_mark_webhook_delivered(&q, s) == AICAM_OK);
    }

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);

    enqueue_one(&q, 10);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 10);
    lc_delivery_queue_clear(&q);
}

static void test_storage_fault_does_not_poison_queue(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);

    g_fs.fail_write_at = g_fs.write_calls + 1;
    g_fs.fail_write_len = 0;
    lc_delivery_meta_t m = { .boot_id = 1, .report_seq = 1,
                             .mqtt = LC_DELIVERY_PENDING,
                             .webhook = LC_DELIVERY_PENDING };
    char buf[64];
    uint32_t n = meta_payload(&m, buf);
    CHECK(lc_delivery_queue_enqueue(&q, &m, buf, n) != AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.storage_faults > 0);

    g_fs.fail_write_at = 0;
    enqueue_one(&q, 2);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 2);
    lc_delivery_queue_clear(&q);
}

static void test_payload_too_large_rejected(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_meta_t m = { .boot_id = 1, .report_seq = 1,
                             .mqtt = LC_DELIVERY_PENDING,
                             .webhook = LC_DELIVERY_PENDING };
    static char big[LC_DQ_SLOT_CAPACITY + 16];
    memset(big, 'x', sizeof(big));
    CHECK(lc_delivery_queue_enqueue(&q, &m, big, sizeof(big)) == AICAM_ERROR_INVALID_DATA);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    lc_delivery_queue_clear(&q);
}

static void test_reclaim_when_all_delivered(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    CHECK(lc_delivery_queue_mark_webhook_delivered(&q, 1) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 2);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 2) == AICAM_OK);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_not_required_reclaims_immediately(void) {
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_meta_t m = { .boot_id = 1, .report_seq = 1,
                             .mqtt = LC_DELIVERY_PENDING,
                             .webhook = LC_DELIVERY_NOT_REQUIRED };
    char buf[64];
    uint32_t n = meta_payload(&m, buf);
    CHECK(lc_delivery_queue_enqueue(&q, &m, buf, n) == AICAM_OK);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    CHECK(st.dropped_webhook == 0);
    lc_delivery_queue_clear(&q);
}

static void test_not_required_state_survives_reopen(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_meta_t m = { .boot_id = 1, .report_seq = 1,
                             .mqtt = LC_DELIVERY_NOT_REQUIRED,
                             .webhook = LC_DELIVERY_PENDING };
    char buf[64];
    uint32_t n = meta_payload(&m, buf);
    CHECK(lc_delivery_queue_enqueue(&q, &m, buf, n) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.mqtt == LC_DELIVERY_NOT_REQUIRED);
    CHECK(out.webhook == LC_DELIVERY_PENDING);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_oldest_pending_per_transport(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);

    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest_for(&q, 0, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_OK);
    CHECK(out.report_seq == 2);
    CHECK(lc_delivery_queue_peek_oldest_for(&q, 1, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_OK);
    CHECK(out.report_seq == 1);

    CHECK(lc_delivery_queue_mark_webhook_delivered(&q, 1) == AICAM_OK);
    CHECK(lc_delivery_queue_peek_oldest_for(&q, 1, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_OK);
    CHECK(out.report_seq == 2);
    lc_delivery_queue_clear(&q);
}

static void test_clear_crash_at_journal_header_write(void);
static void test_clear_crash_at_super_write(void);
static void test_clear_torn_super_recovers_old_queue(void);
static void test_clear_committed_reboot_empty_despite_stale_slots(void);
static void test_clear_gc_failure_still_committed_empty(void);
static void test_evicted_record_not_revived_after_gc_failure(void);
static void test_orphan_slot_without_journal_entry_not_loaded(void);
static void test_one_super_copy_corrupt_still_loads(void);
static void test_evict_tombstone_fail_keeps_old_record(void);
static void test_dropped_counters_durable_across_reinit(void);
static void test_super_and_journal_gen_wrap_across_clears(void);
static void test_drop_baseline_survives_compaction_chain(void);
static void test_compact_failure_keeps_old_journal_and_counts(void);
static void test_clear_resets_durable_drop_baseline(void);
static void test_eviction_compact_super_fail_old_authority(void);
static void test_update_compact_super_fail_state_pre_update(void);

int main(void) {
    test_enqueue_peek_reopen_fifo();
    test_independent_transport_states();
    test_count_overflow_drops_oldest();
    test_byte_overflow_drops_oldest();
    test_clear_reopen_empty();
    test_torn_without_commit_marker();
    test_torn_payload_discarded();
    test_torn_journal_entry_keeps_prior_state();
    test_corrupt_payload_crc_discarded();
    test_journal_compaction_switch();
    test_storage_fault_does_not_poison_queue();
    test_payload_too_large_rejected();
    test_reclaim_when_all_delivered();
    test_not_required_reclaims_immediately();
    test_not_required_state_survives_reopen();
    test_oldest_pending_per_transport();
    test_clear_crash_at_journal_header_write();
    test_clear_crash_at_super_write();
    test_clear_torn_super_recovers_old_queue();
    test_clear_committed_reboot_empty_despite_stale_slots();
    test_clear_gc_failure_still_committed_empty();
    test_evicted_record_not_revived_after_gc_failure();
    test_orphan_slot_without_journal_entry_not_loaded();
    test_one_super_copy_corrupt_still_loads();
    test_evict_tombstone_fail_keeps_old_record();
    test_dropped_counters_durable_across_reinit();
    test_super_and_journal_gen_wrap_across_clears();
    test_drop_baseline_survives_compaction_chain();
    test_compact_failure_keeps_old_journal_and_counts();
    test_clear_resets_durable_drop_baseline();
    test_eviction_compact_super_fail_old_authority();
    test_update_compact_super_fail_state_pre_update();

    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all lc delivery queue tests passed\n");
    return 0;
}

static void test_clear_crash_at_journal_header_write(void) {
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 3; s++) enqueue_one(&q, s);

    g_fs.write_calls = 0;
    g_fs.fail_write_at = 1;
    g_fs.fail_write_len = 0;
    CHECK(lc_delivery_queue_clear(&q) == AICAM_ERROR_IO);
    g_fs.fail_write_at = 0;

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_clear_crash_at_super_write(void) {
    lc_delivery_queue_t q;
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 3; s++) enqueue_one(&q, s);

    g_fs.write_calls = 0;
    g_fs.fail_write_at = 2;
    g_fs.fail_write_len = 0;
    CHECK(lc_delivery_queue_clear(&q) == AICAM_ERROR_IO);
    g_fs.fail_write_at = 0;

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_clear_torn_super_recovers_old_queue(void) {
    lc_delivery_queue_t q;
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 2; s++) enqueue_one(&q, s);

    lc_dq_storage_t ops;
    storage_ops(&ops);
    uint32_t hdr[2] = { 0x4C444A4Eu, 1u };
    CHECK(ops.write(ops.user, LC_DQ_SUPER_SIZE + PHYS_JOURNAL, hdr, sizeof(hdr))
          == AICAM_OK);
    uint8_t torn[LC_DQ_SUPER_COPY];
    memset(torn, 0x77, 10);
    memset(torn + 10, 0xFF, sizeof(torn) - 10);
    CHECK(ops.write(ops.user, 0, torn, sizeof(torn)) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_clear_committed_reboot_empty_despite_stale_slots(void) {
    lc_delivery_queue_t q;
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 3; s++) enqueue_one(&q, s);

    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    CHECK(st.bytes == 0);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_ERROR_NOT_FOUND);
    CHECK(lc_delivery_queue_peek_oldest_for(&q, 0, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_ERROR_NOT_FOUND);
    CHECK(lc_delivery_queue_peek_oldest_for(&q, 1, &out, pbuf, sizeof(pbuf), &plen)
          == AICAM_ERROR_NOT_FOUND);
}

static void test_clear_gc_failure_still_committed_empty(void) {
    lc_delivery_queue_t q;
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    CHECK(q_init(&q) == AICAM_OK);
    for (uint32_t s = 1; s <= 3; s++) enqueue_one(&q, s);

    g_fs.write_calls = 0;
    g_fs.fail_write_at = 3;
    g_fs.fail_write_len = 0;
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
    g_fs.fail_write_at = 0;
    g_fs.fail_write_len = 0;

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.storage_faults > 0);
    CHECK(st.count == 0);

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    lc_delivery_queue_clear(&q);
}

static void test_evicted_record_not_revived_after_gc_failure(void) {
    lc_delivery_queue_t q;
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    lim.max_count = 1;
    CHECK(lc_delivery_queue_init(&q, &ops, &lim) == AICAM_OK);

    enqueue_one(&q, 1);

    g_fs.write_calls = 0;
    g_fs.fail_write_at = 2;
    g_fs.fail_write_len = 0;
    enqueue_one(&q, 2);
    g_fs.fail_write_at = 0;
    g_fs.fail_write_len = 0;

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);
    CHECK(st.storage_faults > 0);

    CHECK(q_init(&q) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 2);
    lc_delivery_queue_clear(&q);
}

static void test_orphan_slot_without_journal_entry_not_loaded(void) {
    lc_delivery_queue_t q;
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);

    uint8_t junk[LC_DQ_JOURNAL_ENTRY];
    memset(junk, 0x5A, sizeof(junk));
    lc_dq_storage_t ops;
    storage_ops(&ops);
    uint32_t entry_off = LC_DQ_SUPER_SIZE + LC_DQ_JOURNAL_HEADER;
    CHECK(ops.write(ops.user, entry_off, junk, sizeof(junk)) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 0);
    lc_delivery_queue_clear(&q);
}

static void test_one_super_copy_corrupt_still_loads(void) {
    lc_delivery_queue_t q;
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);

    uint8_t junk[LC_DQ_SUPER_COPY];
    memset(junk, 0xC7, sizeof(junk));
    lc_dq_storage_t ops;
    storage_ops(&ops);
    uint8_t c0[LC_DQ_SUPER_COPY];
    uint8_t c1[LC_DQ_SUPER_COPY];
    uint32_t seq0 = 0;
    uint32_t seq1 = 0;
    uint8_t v0;
    uint8_t v1;
    CHECK(ops.read(ops.user, 0, c0, sizeof(c0)) == AICAM_OK);
    CHECK(ops.read(ops.user, LC_DQ_SUPER_COPY, c1, sizeof(c1)) == AICAM_OK);
    memcpy(&seq0, c0 + 4, 4);
    memcpy(&seq1, c1 + 4, 4);
    memcpy(&v0, c0, 1);
    memcpy(&v1, c1, 1);
    uint32_t newest_copy;
    uint32_t stale_copy;
    if (v0 == 0x53u && (!v1 || seq0 >= seq1)) {
        newest_copy = 0;
        stale_copy = LC_DQ_SUPER_COPY;
    } else {
        newest_copy = LC_DQ_SUPER_COPY;
        stale_copy = 0;
    }
    (void)newest_copy;
    CHECK(ops.write(ops.user, stale_copy, junk, sizeof(junk)) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    lc_delivery_queue_clear(&q);
}

static void fs_reset(void) {
    memset(g_fs.buf, 0xFF, sizeof(g_fs.buf));
    g_fs.write_calls = 0;
    g_fs.read_calls = 0;
    g_fs.fail_write_at = 0;
    g_fs.fail_write_len = 0;
    g_fs.fail_read_at = 0;
}

static aicam_result_t q_init_count(lc_delivery_queue_t *q, uint32_t max_count) {
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    lim.max_count = max_count;
    return lc_delivery_queue_init(q, &ops, &lim);
}

static aicam_result_t enqueue_raw(lc_delivery_queue_t *q, uint32_t seq) {
    lc_delivery_meta_t m = { .boot_id = 42, .report_seq = seq,
                             .mqtt = LC_DELIVERY_PENDING,
                             .webhook = LC_DELIVERY_PENDING };
    char buf[64];
    uint32_t n = meta_payload(&m, buf);
    return lc_delivery_queue_enqueue(q, &m, buf, n);
}

static void test_evict_tombstone_fail_keeps_old_record(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_count(&q, 2) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);

    g_fs.fail_write_at = g_fs.write_calls + 1;
    CHECK(enqueue_raw(&q, 3) == AICAM_ERROR_IO);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);

    CHECK(q_init_count(&q, 2) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_dropped_counters_durable_across_reinit(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_count(&q, 2) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    CHECK(enqueue_raw(&q, 3) == AICAM_OK);
    CHECK(enqueue_raw(&q, 4) == AICAM_OK);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    CHECK(st.dropped_mqtt == 2);
    CHECK(st.dropped_webhook == 2);

    CHECK(q_init_count(&q, 2) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 2);
    CHECK(st.dropped_mqtt == 2);
    CHECK(st.dropped_webhook == 2);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 3);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_super_and_journal_gen_wrap_across_clears(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init(&q) == AICAM_OK);
    enqueue_one(&q, 1);

    q.super_seq = 0xFFFFFFFEu;
    q.journal_gen[0] = 0xFFFFFFFEu;
    q.journal_gen[1] = 0xFFFFFFFFu;
    q.active_journal = 1;

    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
    CHECK(q.super_seq == 0xFFFFFFFFu);
    CHECK(q.journal_gen[0] == 1u);
    CHECK(enqueue_raw(&q, 2) == AICAM_OK);

    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
    CHECK(q.super_seq == 1u);
    CHECK(q.journal_gen[1] == 2u);
    CHECK(enqueue_raw(&q, 3) == AICAM_OK);

    CHECK(q_init(&q) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 1);
    CHECK(q.super_seq == 1u);
    CHECK(q.active_journal == 1u);
    CHECK(q.journal_gen[1] == 2u);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 3);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static aicam_result_t q_init_lims(lc_delivery_queue_t *q, uint32_t max_count,
                                  uint32_t journal_entries) {
    lc_dq_storage_t ops;
    lc_dq_limits_t lim;
    storage_ops(&ops);
    limits_std(&lim);
    lim.max_count = max_count;
    lim.journal_entries = journal_entries;
    return lc_delivery_queue_init(q, &ops, &lim);
}

static void test_drop_baseline_survives_compaction_chain(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_lims(&q, 3, 3) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    enqueue_one(&q, 3);

    CHECK(enqueue_raw(&q, 4) == AICAM_OK);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 1);
    CHECK(st.dropped_webhook == 1);
    CHECK(st.count == 3);

    CHECK(enqueue_raw(&q, 5) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 2);
    CHECK(st.dropped_webhook == 2);
    CHECK(st.count == 3);

    CHECK(q_init_lims(&q, 3, 32) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 2);
    CHECK(st.dropped_webhook == 2);
    CHECK(st.count == 3);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 3);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_compact_failure_keeps_old_journal_and_counts(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_lims(&q, 3, 3) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    enqueue_one(&q, 3);

    g_fs.fail_write_at = g_fs.write_calls + 2;
    CHECK(enqueue_raw(&q, 4) == AICAM_ERROR_IO);
    g_fs.fail_write_at = 0;

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);
    CHECK(st.count == 3);

    CHECK(q_init_lims(&q, 3, 32) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);
    CHECK(st.count == 3);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_clear_resets_durable_drop_baseline(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_lims(&q, 2, 32) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    CHECK(enqueue_raw(&q, 3) == AICAM_OK);

    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 1);
    CHECK(st.dropped_webhook == 1);

    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);

    CHECK(q_init_lims(&q, 2, 32) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);
    CHECK(st.count == 0);
}

static void test_eviction_compact_super_fail_old_authority(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_lims(&q, 3, 3) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    enqueue_one(&q, 3);
    uint8_t old_active = q.active_journal;

    g_fs.fail_write_at = g_fs.write_calls + 5;
    CHECK(enqueue_raw(&q, 4) == AICAM_ERROR_IO);
    g_fs.fail_write_at = 0;

    {
        uint32_t orphan_off = LC_DQ_SUPER_SIZE
                            + (LC_DQ_JOURNAL_HEADER + LC_DQ_JOURNAL_ENTRY * 128u);
        uint32_t magic = 0, gen = 0;
        memcpy(&magic, g_fs.buf + orphan_off, 4);
        memcpy(&gen, g_fs.buf + orphan_off + 4, 4);
        CHECK(magic == 0x4C444A4Eu);
        CHECK(gen == 1u);
    }

    CHECK(q.active_journal == old_active);
    CHECK(q.stats.dropped_mqtt == 0);
    CHECK(q.stats.dropped_webhook == 0);
    CHECK(q.rec_count == 3);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);
    CHECK(q.active_journal == (uint8_t)(old_active ^ 1u));
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);

    CHECK(q_init_lims(&q, 3, 32) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);

    CHECK(enqueue_raw(&q, 4) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 1);

    CHECK(q_init_lims(&q, 3, 32) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 1);
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 2);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}

static void test_update_compact_super_fail_state_pre_update(void) {
    fs_reset();
    lc_delivery_queue_t q;
    CHECK(q_init_lims(&q, 8, 3) == AICAM_OK);
    enqueue_one(&q, 1);
    enqueue_one(&q, 2);
    enqueue_one(&q, 3);
    uint8_t old_active = q.active_journal;

    g_fs.fail_write_at = g_fs.write_calls + 5;
    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_ERROR_IO);
    g_fs.fail_write_at = 0;

    {
        uint32_t magic = 0, gen = 0;
        uint32_t orphan_off = LC_DQ_SUPER_SIZE
                            + (LC_DQ_JOURNAL_HEADER + LC_DQ_JOURNAL_ENTRY * 128u);
        memcpy(&magic, g_fs.buf + orphan_off, 4);
        memcpy(&gen, g_fs.buf + orphan_off + 4, 4);
        CHECK(magic == 0x4C444A4Eu);
        CHECK(gen == 1u);
    }

    CHECK(q.active_journal == old_active);
    lc_dq_stats_t st;
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    lc_delivery_meta_t out;
    char pbuf[64];
    size_t plen = 0;
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(out.mqtt == LC_DELIVERY_PENDING);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);

    CHECK(lc_delivery_queue_mark_mqtt_delivered(&q, 1) == AICAM_OK);
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);

    CHECK(q_init_lims(&q, 8, 32) == AICAM_OK);
    lc_delivery_queue_get_stats(&q, &st);
    CHECK(st.count == 3);
    CHECK(lc_delivery_queue_peek_oldest(&q, &out, pbuf, sizeof(pbuf), &plen) == AICAM_OK);
    CHECK(out.report_seq == 1);
    CHECK(out.mqtt == LC_DELIVERY_DELIVERED);
    CHECK(st.dropped_mqtt == 0);
    CHECK(st.dropped_webhook == 0);
    CHECK(lc_delivery_queue_clear(&q) == AICAM_OK);
}
