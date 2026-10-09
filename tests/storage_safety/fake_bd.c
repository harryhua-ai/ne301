/*
 * fake_bd.c — RAM-backed fake flash block device with fault injection.
 */
#include "fake_bd.h"
#include <string.h>

#define SCRATCH 1024

static uint8_t rd_buf[SCRATCH] __attribute__((aligned(8)));
static uint8_t pr_buf[SCRATCH] __attribute__((aligned(8)));
static uint8_t la_buf[3072];

/* Device bound by fake_bd_make_config for the offset-based raw reader. */
fake_bd_t *g_raw_bd = 0;

void fake_bd_init(fake_bd_t *bd, uint8_t *mem, size_t block_size, size_t block_count)
{
    memset(bd, 0, sizeof(*bd));
    bd->mem = mem;
    bd->block_size = block_size;
    bd->block_count = block_count;
    bd->fail_reads_from = -1;
    bd->fail_progs_from = -1;
    fake_bd_fill(bd, 0xFF);
}

void fake_bd_reset_counts(fake_bd_t *bd)
{
    bd->read_count = 0;
    bd->prog_count = 0;
    bd->erase_count = 0;
    bd->read_errors = 0;
    bd->prog_errors = 0;
}

void fake_bd_fill(fake_bd_t *bd, uint8_t value)
{
    memset(bd->mem, value, bd->block_size * bd->block_count);
}

void fake_bd_corrupt_block(fake_bd_t *bd, size_t block, uint8_t pattern)
{
    if (block >= bd->block_count) return;
    memset(bd->mem + block * bd->block_size, pattern, bd->block_size);
}

int fake_bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                 void *buffer, lfs_size_t size)
{
    fake_bd_t *bd = (fake_bd_t *)c->context;
    bd->read_count++;
    if (bd->fail_reads_from >= 0 && bd->read_count - 1 >= bd->fail_reads_from) {
        bd->read_errors++;
        return LFS_ERR_IO;
    }
    if (block >= bd->block_count) return LFS_ERR_IO;
    memcpy(buffer, bd->mem + (size_t)block * bd->block_size + off, size);
    return 0;
}

int fake_bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                 const void *buffer, lfs_size_t size)
{
    fake_bd_t *bd = (fake_bd_t *)c->context;
    bd->prog_count++;
    if (bd->fail_progs_from >= 0 && bd->prog_count - 1 >= bd->fail_progs_from) {
        bd->prog_errors++;
        return LFS_ERR_IO;
    }
    if (block >= bd->block_count) return LFS_ERR_IO;
    memcpy(bd->mem + (size_t)block * bd->block_size + off, buffer, size);
    return 0;
}

int fake_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    fake_bd_t *bd = (fake_bd_t *)c->context;
    bd->erase_count++;
    if (block >= bd->block_count) return LFS_ERR_IO;
    memset(bd->mem + (size_t)block * bd->block_size, 0xFF, bd->block_size);
    return 0;
}

int fake_bd_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

int fake_bd_raw_read(uint32_t offset, void *data, size_t len)
{
    if (!g_raw_bd) return -1;
    g_raw_bd->read_count++;
    if (g_raw_bd->fail_reads_from >= 0 &&
        g_raw_bd->read_count - 1 >= g_raw_bd->fail_reads_from) {
        g_raw_bd->read_errors++;
        return -1;
    }
    if ((size_t)offset + len > g_raw_bd->block_size * g_raw_bd->block_count) return -1;
    memcpy(data, g_raw_bd->mem + offset, len);
    return 0;
}

void fake_bd_make_config(fake_bd_t *bd, struct lfs_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->context = bd;
    cfg->read = fake_bd_read;
    cfg->prog = fake_bd_prog;
    cfg->erase = fake_bd_erase;
    cfg->sync = fake_bd_sync;
    cfg->read_size = 256;
    cfg->prog_size = 256;
    cfg->block_size = bd->block_size;
    cfg->block_count = (lfs_size_t)bd->block_count;
    cfg->block_cycles = 10000;
    cfg->cache_size = 256;
    cfg->lookahead_size = sizeof(la_buf);
    cfg->read_buffer = rd_buf;
    cfg->prog_buffer = pr_buf;
    cfg->lookahead_buffer = la_buf;
    g_raw_bd = bd;
}
