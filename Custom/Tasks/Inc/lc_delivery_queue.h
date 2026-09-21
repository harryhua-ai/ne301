#ifndef LC_DELIVERY_QUEUE_H
#define LC_DELIVERY_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include "aicam_types.h"

#define LC_DQ_MAX_SLOTS          64u
#define LC_DQ_SLOT_CAPACITY      6144u
#define LC_DQ_JOURNAL_HEADER     16u
#define LC_DQ_JOURNAL_ENTRY      16u
#define LC_DQ_SUPER_SIZE         16u
#define LC_DQ_SLOT_HEADER_SIZE   32u

#define LC_DQ_REGION_SIZE \
    (LC_DQ_SUPER_SIZE + 2u * (LC_DQ_JOURNAL_HEADER + LC_DQ_JOURNAL_ENTRY * 128u) \
     + LC_DQ_MAX_SLOTS * (LC_DQ_SLOT_HEADER_SIZE + LC_DQ_SLOT_CAPACITY))

typedef enum {
    LC_DELIVERY_NOT_REQUIRED = 0,
    LC_DELIVERY_PENDING,
    LC_DELIVERY_DELIVERED
} lc_delivery_state_t;

typedef struct {
    uint32_t            boot_id;
    uint32_t            report_seq;
    lc_delivery_state_t mqtt;
    lc_delivery_state_t webhook;
} lc_delivery_meta_t;

typedef struct {
    uint16_t max_count;
    uint16_t journal_entries;
    uint32_t max_bytes;
} lc_dq_limits_t;

typedef struct {
    void *user;
    aicam_result_t (*read)(void *user, uint32_t offset, void *buf, uint32_t len);
    aicam_result_t (*write)(void *user, uint32_t offset, const void *buf, uint32_t len);
    aicam_result_t (*erase_region)(void *user);
} lc_dq_storage_t;

typedef struct {
    uint32_t count;
    uint32_t bytes;
    uint32_t dropped_mqtt;
    uint32_t dropped_webhook;
    uint32_t storage_faults;
} lc_dq_stats_t;

typedef struct {
    uint32_t            slot_seq;
    lc_delivery_state_t mqtt;
    lc_delivery_state_t webhook;
    uint16_t            slot_index;
    uint16_t            rsv;
} lc_dq_rec_t;

typedef struct lc_delivery_queue {
    lc_dq_storage_t storage;
    lc_dq_limits_t  limits;
    uint8_t         active_journal;
    uint8_t         rsv[3];
    uint32_t        journal_gen[2];
    uint32_t        super_seq;
    lc_dq_rec_t     recs[LC_DQ_MAX_SLOTS];
    uint16_t        rec_count;
    uint32_t        bytes_used;
    uint32_t        next_entry[2];
    lc_dq_stats_t   stats;
} lc_delivery_queue_t;

aicam_result_t lc_delivery_queue_init(lc_delivery_queue_t *q, const lc_dq_storage_t *storage,
                                      const lc_dq_limits_t *limits);
aicam_result_t lc_delivery_queue_enqueue(lc_delivery_queue_t *q, const lc_delivery_meta_t *meta,
                                         const char *payload, size_t payload_len);
aicam_result_t lc_delivery_queue_peek_oldest(lc_delivery_queue_t *q, lc_delivery_meta_t *out,
                                             char *payload_buf, size_t buf_size,
                                             size_t *payload_len_out);
aicam_result_t lc_delivery_queue_peek_oldest_for(lc_delivery_queue_t *q, uint8_t transport,
                                                 lc_delivery_meta_t *out, char *payload_buf,
                                                 size_t buf_size, size_t *payload_len_out);
aicam_result_t lc_delivery_queue_mark_mqtt_delivered(lc_delivery_queue_t *q, uint32_t report_seq);
aicam_result_t lc_delivery_queue_mark_webhook_delivered(lc_delivery_queue_t *q,
                                                        uint32_t report_seq);
aicam_result_t lc_delivery_queue_clear(lc_delivery_queue_t *q);
void           lc_delivery_queue_get_stats(const lc_delivery_queue_t *q, lc_dq_stats_t *out);

#endif
