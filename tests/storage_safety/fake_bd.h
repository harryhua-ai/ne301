/*
 * fake_bd.h — RAM-backed fake flash block device with fault injection for
 * host error-injection tests (issue #37 AC4).
 */
#ifndef FAKE_BD_H
#define FAKE_BD_H

#include <stddef.h>
#include <stdint.h>
#include "lfs.h"

typedef struct {
    uint8_t *mem;            /* block_size * block_count bytes */
    size_t   block_size;
    size_t   block_count;
    int      fail_reads_from;  /* fail read calls whose index >= this (-1 never) */
    int      fail_progs_from;  /* fail prog calls whose index >= this (-1 never) */
    long     read_count;
    long     prog_count;
    long     erase_count;
    long     read_errors;      /* times a read was made to fail */
    long     prog_errors;
} fake_bd_t;

void fake_bd_init(fake_bd_t *bd, uint8_t *mem, size_t block_size, size_t block_count);
void fake_bd_reset_counts(fake_bd_t *bd);
void fake_bd_fill(fake_bd_t *bd, uint8_t value);       /* erase whole device */
void fake_bd_corrupt_block(fake_bd_t *bd, size_t block, uint8_t pattern);

/* littlefs callbacks */
int fake_bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                 void *buffer, lfs_size_t size);
int fake_bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                 const void *buffer, lfs_size_t size);
int fake_bd_erase(const struct lfs_config *c, lfs_block_t block);
int fake_bd_sync(const struct lfs_config *c);

/* storage_raw_read_t-shaped wrapper over the fake device (offset-based) */
int fake_bd_raw_read(uint32_t offset, void *data, size_t len);

/* Build a ready-to-use littlefs config bound to bd (static buffers inside). */
void fake_bd_make_config(fake_bd_t *bd, struct lfs_config *cfg);

#endif /* FAKE_BD_H */
