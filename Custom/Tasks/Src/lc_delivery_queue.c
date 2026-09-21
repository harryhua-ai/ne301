#include "lc_delivery_queue.h"
#include <string.h>

#define LC_DQ_SUPER_MAGIC     0x4C445153u
#define LC_DQ_JOURNAL_MAGIC   0x4C444A4Eu
#define LC_DQ_ENTRY_MAGIC     0x4C444A45u
#define LC_DQ_SLOT_MAGIC      0x4C44534Cu
#define LC_DQ_COMMIT_MARKER   0x434D5431u
#define LC_DQ_ERASED_U32      0xFFFFFFFFu

#define LC_DQ_JOURNAL_REGION  (LC_DQ_JOURNAL_HEADER + LC_DQ_JOURNAL_ENTRY * 128u)
#define LC_DQ_SLOT_STRIDE     (LC_DQ_SLOT_HEADER_SIZE + LC_DQ_SLOT_CAPACITY)

typedef struct {
    uint32_t magic;
    uint32_t super_seq;
    uint8_t  active;
    uint8_t  rsv[3];
    uint32_t crc;
} lc_dq_super_t;

typedef struct {
    uint32_t magic;
    uint32_t gen;
} lc_dq_journal_hdr_t;

typedef struct {
    uint32_t magic;
    uint32_t slot_seq;
    uint8_t  mqtt;
    uint8_t  webhook;
    uint8_t  rsv[2];
    uint32_t crc;
} lc_dq_journal_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t slot_seq;
    uint32_t payload_len;
    uint32_t payload_crc;
    uint8_t  mqtt;
    uint8_t  webhook;
    uint8_t  rsv[2];
    uint32_t boot_id;
    uint32_t rsv2;
    uint32_t commit;
} lc_dq_slot_hdr_t;

static uint32_t lc_dq_crc_begin(void) { return 0xFFFFFFFFu; }

static uint32_t lc_dq_crc_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc;
}

static uint32_t lc_dq_crc32(const void *data, size_t len) {
    return lc_dq_crc_update(lc_dq_crc_begin(), data, len) ^ 0xFFFFFFFFu;
}

static uint32_t lc_dq_journal_offset(uint8_t region) {
    return LC_DQ_SUPER_SIZE + (uint32_t)region * LC_DQ_JOURNAL_REGION;
}

static uint32_t lc_dq_slot_offset(uint16_t idx) {
    return LC_DQ_SUPER_SIZE + 2u * LC_DQ_JOURNAL_REGION + (uint32_t)idx * LC_DQ_SLOT_STRIDE;
}

static aicam_result_t lc_dq_read(lc_delivery_queue_t *q, uint32_t off, void *buf, uint32_t len) {
    aicam_result_t r = q->storage.read(q->storage.user, off, buf, len);
    if (r != AICAM_OK) q->stats.storage_faults++;
    return r;
}

static aicam_result_t lc_dq_write(lc_delivery_queue_t *q, uint32_t off, const void *buf,
                                  uint32_t len) {
    aicam_result_t r = q->storage.write(q->storage.user, off, buf, len);
    if (r != AICAM_OK) q->stats.storage_faults++;
    return r;
}

static aicam_result_t lc_dq_write_super(lc_delivery_queue_t *q) {
    lc_dq_super_t s;
    s.magic = LC_DQ_SUPER_MAGIC;
    s.super_seq = ++q->super_seq;
    s.active = q->active_journal;
    memset(s.rsv, 0, sizeof(s.rsv));
    s.crc = lc_dq_crc32(&s, offsetof(lc_dq_super_t, crc));
    return lc_dq_write(q, 0, &s, sizeof(s));
}

static void lc_dq_entry_fill(lc_dq_journal_entry_t *e, const lc_dq_rec_t *r) {
    e->magic = LC_DQ_ENTRY_MAGIC;
    e->slot_seq = r->slot_seq;
    e->mqtt = (uint8_t)r->mqtt;
    e->webhook = (uint8_t)r->webhook;
    memset(e->rsv, 0, sizeof(e->rsv));
    e->crc = lc_dq_crc32(e, offsetof(lc_dq_journal_entry_t, crc));
}

static int16_t lc_dq_find(const lc_delivery_queue_t *q, uint32_t slot_seq) {
    for (uint16_t i = 0; i < q->rec_count; i++) {
        if (q->recs[i].slot_seq == slot_seq) return (int16_t)i;
    }
    return -1;
}

static int16_t lc_dq_find_oldest(const lc_delivery_queue_t *q) {
    int16_t oldest = -1;
    for (uint16_t i = 0; i < q->rec_count; i++) {
        if (oldest < 0 || q->recs[i].slot_seq < q->recs[oldest].slot_seq) oldest = (int16_t)i;
    }
    return oldest;
}

static int16_t lc_dq_find_free_slot(const lc_delivery_queue_t *q) {
    for (uint16_t idx = 0; idx < LC_DQ_MAX_SLOTS; idx++) {
        int16_t at = -1;
        for (uint16_t i = 0; i < q->rec_count; i++) {
            if (q->recs[i].slot_index == (uint16_t)idx) { at = (int16_t)i; break; }
        }
        if (at < 0) return (int16_t)idx;
    }
    return -1;
}

static aicam_result_t lc_dq_invalidate_slot(lc_delivery_queue_t *q, uint16_t slot_index) {
    uint32_t magic = LC_DQ_ERASED_U32;
    return lc_dq_write(q, lc_dq_slot_offset(slot_index), &magic, sizeof(magic));
}

static void lc_dq_remove_rec(lc_delivery_queue_t *q, uint16_t at) {
    lc_dq_slot_hdr_t hdr;
    if (lc_dq_read(q, lc_dq_slot_offset(q->recs[at].slot_index), &hdr, sizeof(hdr)) == AICAM_OK &&
        hdr.payload_len <= LC_DQ_SLOT_CAPACITY) {
        q->bytes_used -= hdr.payload_len;
    }
    for (uint16_t i = at; i + 1 < q->rec_count; i++) q->recs[i] = q->recs[i + 1];
    q->rec_count--;
}

static aicam_result_t lc_dq_compact(lc_delivery_queue_t *q, const lc_dq_rec_t *update) {
    uint8_t region = q->active_journal;
    uint8_t other = (uint8_t)(region ^ 1u);
    lc_dq_journal_hdr_t h = { .magic = LC_DQ_JOURNAL_MAGIC,
                              .gen = q->journal_gen[region] + 1u };
    aicam_result_t res = lc_dq_write(q, lc_dq_journal_offset(other), &h, sizeof(h));
    if (res != AICAM_OK) return res;
    q->next_entry[other] = 0;

    for (uint16_t i = 0; i < q->rec_count; i++) {
        if (q->recs[i].slot_seq == update->slot_seq) continue;
        lc_dq_journal_entry_t e;
        lc_dq_entry_fill(&e, &q->recs[i]);
        uint32_t off = lc_dq_journal_offset(other) + LC_DQ_JOURNAL_HEADER
                     + q->next_entry[other] * LC_DQ_JOURNAL_ENTRY;
        res = lc_dq_write(q, off, &e, sizeof(e));
        if (res != AICAM_OK) return res;
        q->next_entry[other]++;
    }

    lc_dq_journal_entry_t e;
    lc_dq_entry_fill(&e, update);
    uint32_t off = lc_dq_journal_offset(other) + LC_DQ_JOURNAL_HEADER
                 + q->next_entry[other] * LC_DQ_JOURNAL_ENTRY;
    res = lc_dq_write(q, off, &e, sizeof(e));
    if (res != AICAM_OK) return res;
    q->next_entry[other]++;

    q->active_journal = other;
    q->journal_gen[other] = h.gen;
    res = lc_dq_write_super(q);
    return res;
}

static aicam_result_t lc_dq_append_entry(lc_delivery_queue_t *q, const lc_dq_rec_t *r) {
    if (q->next_entry[q->active_journal] >= q->limits.journal_entries) {
        return lc_dq_compact(q, r);
    }

    uint8_t region = q->active_journal;
    lc_dq_journal_entry_t e;
    lc_dq_entry_fill(&e, r);
    uint32_t off = lc_dq_journal_offset(region) + LC_DQ_JOURNAL_HEADER
                 + q->next_entry[region] * LC_DQ_JOURNAL_ENTRY;
    aicam_result_t res = lc_dq_write(q, off, &e, sizeof(e));
    if (res != AICAM_OK) return res;
    q->next_entry[region]++;
    return AICAM_OK;
}

static void lc_dq_replay_journal(lc_delivery_queue_t *q, uint8_t region) {
    uint32_t base = lc_dq_journal_offset(region) + LC_DQ_JOURNAL_HEADER;
    uint32_t valid = 0;
    for (uint32_t i = 0; i < q->limits.journal_entries; i++) {
        lc_dq_journal_entry_t e;
        if (lc_dq_read(q, base + i * LC_DQ_JOURNAL_ENTRY, &e, sizeof(e)) != AICAM_OK) break;
        if (e.magic != LC_DQ_ENTRY_MAGIC) break;
        if (e.crc != lc_dq_crc32(&e, offsetof(lc_dq_journal_entry_t, crc))) break;
        int16_t at = lc_dq_find(q, e.slot_seq);
        if (at >= 0) {
            if (e.mqtt <= LC_DELIVERY_DELIVERED) q->recs[at].mqtt = (lc_delivery_state_t)e.mqtt;
            if (e.webhook <= LC_DELIVERY_DELIVERED) {
                q->recs[at].webhook = (lc_delivery_state_t)e.webhook;
            }
        }
        valid = i + 1u;
    }
    q->next_entry[region] = valid;
}

aicam_result_t lc_delivery_queue_init(lc_delivery_queue_t *q, const lc_dq_storage_t *storage,
                                      const lc_dq_limits_t *limits) {
    if (!q || !storage || !storage->read || !storage->write || !limits) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (limits->journal_entries == 0 || limits->journal_entries > 128u) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (limits->max_count == 0) return AICAM_ERROR_INVALID_PARAM;
    memset(q, 0, sizeof(*q));
    q->storage = *storage;
    q->limits = *limits;

    for (uint8_t r = 0; r < 2u; r++) {
        lc_dq_journal_hdr_t h;
        if (lc_dq_read(q, lc_dq_journal_offset(r), &h, sizeof(h)) == AICAM_OK &&
            h.magic == LC_DQ_JOURNAL_MAGIC) {
            q->journal_gen[r] = h.gen;
        }
    }

    lc_dq_super_t s;
    uint8_t active = 0;
    uint8_t super_valid = 0;
    if (lc_dq_read(q, 0, &s, sizeof(s)) == AICAM_OK && s.magic == LC_DQ_SUPER_MAGIC &&
        s.active <= 1u && s.crc == lc_dq_crc32(&s, offsetof(lc_dq_super_t, crc))) {
        active = s.active;
        super_valid = 1;
        q->super_seq = s.super_seq;
    } else {
        active = (q->journal_gen[1] > q->journal_gen[0]) ? 1u : 0u;
    }
    q->active_journal = active;

    for (uint16_t idx = 0; idx < LC_DQ_MAX_SLOTS; idx++) {
        lc_dq_slot_hdr_t h;
        if (lc_dq_read(q, lc_dq_slot_offset(idx), &h, sizeof(h)) != AICAM_OK) break;
        if (h.magic != LC_DQ_SLOT_MAGIC || h.commit != LC_DQ_COMMIT_MARKER) continue;
        if (h.payload_len == 0 || h.payload_len > LC_DQ_SLOT_CAPACITY) continue;
        if (h.slot_seq == 0) continue;

        uint8_t buf[256];
        uint32_t crc = lc_dq_crc_begin();
        uint32_t remaining = h.payload_len;
        uint32_t off = lc_dq_slot_offset(idx) + LC_DQ_SLOT_HEADER_SIZE;
        uint8_t bad = 0;
        while (remaining > 0) {
            uint32_t chunk = remaining < sizeof(buf) ? remaining : (uint32_t)sizeof(buf);
            if (lc_dq_read(q, off, buf, chunk) != AICAM_OK) { bad = 1; break; }
            crc = lc_dq_crc_update(crc, buf, chunk);
            remaining -= chunk;
            off += chunk;
        }
        if (bad) continue;
        if ((crc ^ 0xFFFFFFFFu) != h.payload_crc) continue;

        if (q->rec_count >= LC_DQ_MAX_SLOTS) break;
        lc_dq_rec_t *r = &q->recs[q->rec_count++];
        r->slot_seq = h.slot_seq;
        r->mqtt = (h.mqtt <= LC_DELIVERY_DELIVERED) ? (lc_delivery_state_t)h.mqtt
                                                    : LC_DELIVERY_PENDING;
        r->webhook = (h.webhook <= LC_DELIVERY_DELIVERED) ? (lc_delivery_state_t)h.webhook
                                                          : LC_DELIVERY_PENDING;
        r->slot_index = idx;
        q->bytes_used += h.payload_len;
    }

    lc_dq_replay_journal(q, active);

    for (uint16_t i = 0; i + 1 < q->rec_count; i++) {
        for (uint16_t j = (uint16_t)(i + 1); j < q->rec_count; j++) {
            if (q->recs[j].slot_seq < q->recs[i].slot_seq) {
                lc_dq_rec_t t = q->recs[i];
                q->recs[i] = q->recs[j];
                q->recs[j] = t;
            }
        }
    }

    if (!super_valid) {
        (void)lc_dq_write_super(q);
    }
    return AICAM_OK;
}

aicam_result_t lc_delivery_queue_enqueue(lc_delivery_queue_t *q, const lc_delivery_meta_t *meta,
                                         const char *payload, size_t payload_len) {
    if (!q || !meta || !payload || payload_len == 0) return AICAM_ERROR_INVALID_PARAM;
    if (payload_len > LC_DQ_SLOT_CAPACITY) return AICAM_ERROR_INVALID_DATA;
    if (meta->mqtt > LC_DELIVERY_DELIVERED || meta->webhook > LC_DELIVERY_DELIVERED) {
        return AICAM_ERROR_INVALID_DATA;
    }

    while (q->rec_count > 0 &&
           (q->rec_count >= q->limits.max_count ||
            q->bytes_used + payload_len > q->limits.max_bytes)) {
        int16_t oldest = lc_dq_find_oldest(q);
        if (oldest < 0) break;
        lc_dq_rec_t victim = q->recs[oldest];
        if (victim.mqtt == LC_DELIVERY_PENDING) q->stats.dropped_mqtt++;
        if (victim.webhook == LC_DELIVERY_PENDING) q->stats.dropped_webhook++;
        lc_dq_remove_rec(q, (uint16_t)oldest);
        (void)lc_dq_invalidate_slot(q, victim.slot_index);
    }

    int16_t slot_idx = lc_dq_find_free_slot(q);
    if (slot_idx < 0) return AICAM_ERROR_NO_MEMORY;

    uint32_t crc = lc_dq_crc32(payload, payload_len);
    uint32_t base = lc_dq_slot_offset((uint16_t)slot_idx);

    aicam_result_t res = lc_dq_write(q, base + LC_DQ_SLOT_HEADER_SIZE, payload,
                                     (uint32_t)payload_len);
    if (res != AICAM_OK) return res;

    lc_dq_slot_hdr_t h;
    h.magic = LC_DQ_SLOT_MAGIC;
    h.slot_seq = meta->report_seq;
    h.payload_len = (uint32_t)payload_len;
    h.payload_crc = crc;
    h.mqtt = (uint8_t)meta->mqtt;
    h.webhook = (uint8_t)meta->webhook;
    memset(h.rsv, 0, sizeof(h.rsv));
    h.boot_id = meta->boot_id;
    h.rsv2 = LC_DQ_ERASED_U32;
    h.commit = LC_DQ_ERASED_U32;
    res = lc_dq_write(q, base, &h, sizeof(h));
    if (res != AICAM_OK) return res;

    uint32_t marker = LC_DQ_COMMIT_MARKER;
    res = lc_dq_write(q, base + offsetof(lc_dq_slot_hdr_t, commit), &marker, sizeof(marker));
    if (res != AICAM_OK) return res;

    lc_dq_rec_t rec;
    rec.slot_seq = meta->report_seq;
    rec.mqtt = meta->mqtt;
    rec.webhook = meta->webhook;
    rec.slot_index = (uint16_t)slot_idx;

    res = lc_dq_append_entry(q, &rec);
    if (res != AICAM_OK) return res;

    q->recs[q->rec_count++] = rec;
    q->bytes_used += (uint32_t)payload_len;
    return AICAM_OK;
}

aicam_result_t lc_delivery_queue_peek_oldest(lc_delivery_queue_t *q, lc_delivery_meta_t *out,
                                             char *payload_buf, size_t buf_size,
                                             size_t *payload_len_out) {
    if (!q || !out) return AICAM_ERROR_INVALID_PARAM;
    int16_t oldest = lc_dq_find_oldest(q);
    if (oldest < 0) return AICAM_ERROR_NOT_FOUND;
    lc_dq_rec_t *r = &q->recs[oldest];

    lc_dq_slot_hdr_t h;
    aicam_result_t res = lc_dq_read(q, lc_dq_slot_offset(r->slot_index), &h, sizeof(h));
    if (res != AICAM_OK) return res;

    out->boot_id = h.boot_id;
    out->report_seq = r->slot_seq;
    out->mqtt = r->mqtt;
    out->webhook = r->webhook;

    if (payload_buf && payload_len_out) {
        if (buf_size < h.payload_len) return AICAM_ERROR_INVALID_PARAM;
        res = lc_dq_read(q, lc_dq_slot_offset(r->slot_index) + LC_DQ_SLOT_HEADER_SIZE,
                         payload_buf, h.payload_len);
        if (res != AICAM_OK) return res;
        *payload_len_out = h.payload_len;
    }
    return AICAM_OK;
}

static int16_t lc_dq_find_oldest_pending(const lc_delivery_queue_t *q, uint8_t transport) {
    int16_t found = -1;
    for (uint16_t i = 0; i < q->rec_count; i++) {
        lc_delivery_state_t st = (transport == 0u) ? q->recs[i].mqtt : q->recs[i].webhook;
        if (st != LC_DELIVERY_PENDING) continue;
        if (found < 0 || q->recs[i].slot_seq < q->recs[found].slot_seq) found = (int16_t)i;
    }
    return found;
}

aicam_result_t lc_delivery_queue_peek_oldest_for(lc_delivery_queue_t *q, uint8_t transport,
                                                 lc_delivery_meta_t *out, char *payload_buf,
                                                 size_t buf_size, size_t *payload_len_out) {
    if (!q || !out || transport > 1u) return AICAM_ERROR_INVALID_PARAM;
    int16_t at = lc_dq_find_oldest_pending(q, transport);
    if (at < 0) return AICAM_ERROR_NOT_FOUND;
    lc_dq_rec_t *r = &q->recs[at];

    lc_dq_slot_hdr_t h;
    aicam_result_t res = lc_dq_read(q, lc_dq_slot_offset(r->slot_index), &h, sizeof(h));
    if (res != AICAM_OK) return res;

    out->boot_id = h.boot_id;
    out->report_seq = r->slot_seq;
    out->mqtt = r->mqtt;
    out->webhook = r->webhook;

    if (payload_buf && payload_len_out) {
        if (buf_size < h.payload_len) return AICAM_ERROR_INVALID_PARAM;
        res = lc_dq_read(q, lc_dq_slot_offset(r->slot_index) + LC_DQ_SLOT_HEADER_SIZE,
                         payload_buf, h.payload_len);
        if (res != AICAM_OK) return res;
        *payload_len_out = h.payload_len;
    }
    return AICAM_OK;
}

static aicam_result_t lc_dq_mark(lc_delivery_queue_t *q, uint32_t report_seq,
                                 uint8_t which) {
    int16_t at = lc_dq_find(q, report_seq);
    if (at < 0) return AICAM_ERROR_NOT_FOUND;

    lc_dq_rec_t updated = q->recs[at];
    if (which == 0) updated.mqtt = LC_DELIVERY_DELIVERED;
    else updated.webhook = LC_DELIVERY_DELIVERED;

    aicam_result_t res = lc_dq_append_entry(q, &updated);
    if (res != AICAM_OK) return res;

    q->recs[at].mqtt = updated.mqtt;
    q->recs[at].webhook = updated.webhook;

    if (q->recs[at].mqtt != LC_DELIVERY_PENDING &&
        q->recs[at].webhook != LC_DELIVERY_PENDING) {
        uint16_t slot_index = q->recs[at].slot_index;
        lc_dq_remove_rec(q, (uint16_t)at);
        (void)lc_dq_invalidate_slot(q, slot_index);
    }
    return AICAM_OK;
}

aicam_result_t lc_delivery_queue_mark_mqtt_delivered(lc_delivery_queue_t *q,
                                                     uint32_t report_seq) {
    if (!q) return AICAM_ERROR_INVALID_PARAM;
    return lc_dq_mark(q, report_seq, 0);
}

aicam_result_t lc_delivery_queue_mark_webhook_delivered(lc_delivery_queue_t *q,
                                                        uint32_t report_seq) {
    if (!q) return AICAM_ERROR_INVALID_PARAM;
    return lc_dq_mark(q, report_seq, 1);
}

aicam_result_t lc_delivery_queue_clear(lc_delivery_queue_t *q) {
    if (!q) return AICAM_ERROR_INVALID_PARAM;
    while (q->rec_count > 0) {
        uint16_t slot_index = q->recs[q->rec_count - 1].slot_index;
        q->bytes_used = 0;
        q->rec_count--;
        (void)lc_dq_invalidate_slot(q, slot_index);
    }
    q->bytes_used = 0;
    q->stats.dropped_mqtt = 0;
    q->stats.dropped_webhook = 0;
    for (uint8_t r = 0; r < 2u; r++) {
        lc_dq_journal_hdr_t h = { .magic = LC_DQ_JOURNAL_MAGIC,
                                  .gen = ++q->journal_gen[r] };
        aicam_result_t res = lc_dq_write(q, lc_dq_journal_offset(r), &h, sizeof(h));
        if (res != AICAM_OK) return res;
        q->next_entry[r] = 0;
    }
    return lc_dq_write_super(q);
}

void lc_delivery_queue_get_stats(const lc_delivery_queue_t *q, lc_dq_stats_t *out) {
    if (!q || !out) return;
    *out = q->stats;
    out->count = q->rec_count;
    out->bytes = q->bytes_used;
    out->mqtt_pending = 0;
    out->webhook_pending = 0;
    for (uint16_t i = 0; i < q->rec_count; i++) {
        if (q->recs[i].mqtt == LC_DELIVERY_PENDING) out->mqtt_pending++;
        if (q->recs[i].webhook == LC_DELIVERY_PENDING) out->webhook_pending++;
    }
}
