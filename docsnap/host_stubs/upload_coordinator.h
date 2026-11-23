#ifndef STUB_UC_H
#define STUB_UC_H
#include "aicam_types.h"
#include <stdint.h>
typedef enum { RECORD_STATE_PENDING=0, RECORD_STATE_SENT=1, RECORD_STATE_FAILED=2,
  RECORD_STATE_LOCAL=3 } record_state_t;
typedef struct { char id[64]; record_state_t state; uint64_t timestamp; uint32_t size;
  uint8_t retry_count; char trigger[16]; char last_error[64]; char image_id[64];
  aicam_bool_t has_inference; } record_info_t;
int upload_coordinator_list_records(record_state_t filter, uint32_t offset, uint32_t limit,
  uint64_t from_ts, uint64_t to_ts, aicam_bool_t sort_desc, record_info_t *out, int max);
#endif
