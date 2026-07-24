#include "pc_backlog.h"
#include "storage.h"      /* flash_lfs_* + lfs.h (struct lfs_info, LFS_TYPE_REG) */
#include <stdio.h>
#include <string.h>

static const char* CHNAME[] = { "mqtt", "webhook" };
static const char* DIR_FMT  = "/pc_backlog/%s";          /* e.g. /pc_backlog/mqtt */
static const char* FILE_FMT = "/pc_backlog/%s/%08lu.json";
static const char* SEQ_FMT  = "/pc_backlog/%s/_seq";

const char* backlog_channel_name(backlog_channel_t ch) {
    return (ch < BACKLOG_CHANNEL_COUNT) ? CHNAME[ch] : "unknown";
}

/* seq counter persisted in /pc_backlog/<ch>/_seq as 4 raw bytes (network order agnostic — just a counter). */
static uint32_t read_seq(backlog_channel_t ch) {
    char path[64]; snprintf(path, sizeof(path), SEQ_FMT, CHNAME[ch]);
    void* fd = flash_lfs_fopen(path, "r");
    if (!fd) return 0;
    uint32_t s = 0;
    int n = flash_lfs_fread(fd, &s, sizeof(s));
    (void)n;
    flash_lfs_fclose(fd);
    return s;
}
/* Returns 0 on success, -1 if the seq counter could not be persisted.
 * Keeping seq consistent is essential: if it doesn't advance, the next push
 * would overwrite this file. Callers must roll back the data file on failure. */
static int write_seq(backlog_channel_t ch, uint32_t s) {
    char path[64]; snprintf(path, sizeof(path), SEQ_FMT, CHNAME[ch]);
    void* fd = flash_lfs_fopen(path, "w");
    if (!fd) return -1;
    int w = flash_lfs_fwrite(fd, &s, sizeof(s));
    flash_lfs_fclose(fd);
    return (w == (int)sizeof(s)) ? 0 : -1;
}

/* Single-pass scan: find the lexicographically smallest regular-file name in /pc_backlog/<ch>
 * (filenames are zero-padded seq, so lex order == age order). Copies the name (not full path)
 * into name_out. Returns 1 if found, 0 if empty. Also used by count() to just tally. */
static int find_oldest_name(backlog_channel_t ch, char* name_out, size_t name_out_len) {
    char dirpath[48]; snprintf(dirpath, sizeof(dirpath), DIR_FMT, CHNAME[ch]);
    void* dd = flash_lfs_opendir(dirpath);
    if (!dd) return 0;
    int found = 0;
    char best[LFS_NAME_MAX + 1];
    best[0] = '\0';
    struct lfs_info info;
    while (flash_lfs_readdir(dd, (char*)&info) > 0) {
        if (info.type != LFS_TYPE_REG) continue;          /* skip "." ".." and dirs */
        if (strcmp(info.name, "_seq") == 0) continue;     /* skip seq counter */
        if (!found || strcmp(info.name, best) < 0) {
            strncpy(best, info.name, sizeof(best) - 1);
            best[sizeof(best) - 1] = '\0';
            found = 1;
        }
    }
    flash_lfs_closedir(dd);
    if (found && name_out && name_out_len) {
        strncpy(name_out, best, name_out_len - 1);
        name_out[name_out_len - 1] = '\0';
    }
    return found;
}

aicam_result_t backlog_push(backlog_channel_t ch, const char* json, uint16_t capacity) {
    if (!json) return AICAM_ERROR_INVALID_PARAM;
    char path[64]; uint32_t seq = read_seq(ch);
    snprintf(path, sizeof(path), FILE_FMT, CHNAME[ch], (unsigned long)seq);
    void* fd = flash_lfs_fopen(path, "w");
    if (!fd) return AICAM_ERROR_IO;
    size_t len = strlen(json);
    int w = flash_lfs_fwrite(fd, json, len);
    flash_lfs_fclose(fd);
    if (w < 0) return AICAM_ERROR_IO;
    if (write_seq(ch, seq + 1) != 0) {
        /* seq didn't advance — roll back the data file so the next push doesn't overwrite it */
        flash_lfs_remove(path);
        return AICAM_ERROR_IO;
    }
    /* enforce capacity */
    while (backlog_count(ch) > capacity) backlog_drop_oldest(ch);
    return AICAM_OK;
}

aicam_result_t backlog_pop(backlog_channel_t ch, char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return AICAM_ERROR_INVALID_PARAM;
    char name[LFS_NAME_MAX + 1];
    if (!find_oldest_name(ch, name, sizeof(name))) return AICAM_ERROR_NOT_FOUND;
    char path[320]; snprintf(path, sizeof(path), "/pc_backlog/%s/%s", CHNAME[ch], name);
    void* fd = flash_lfs_fopen(path, "r");
    if (!fd) return AICAM_ERROR_IO;
    int rd = flash_lfs_fread(fd, buf, buf_len - 1);
    flash_lfs_fclose(fd);
    flash_lfs_remove(path);
    if (rd < 0) rd = 0;
    buf[rd] = '\0';
    return AICAM_OK;
}

uint16_t backlog_count(backlog_channel_t ch) {
    char dirpath[48]; snprintf(dirpath, sizeof(dirpath), DIR_FMT, CHNAME[ch]);
    void* dd = flash_lfs_opendir(dirpath);
    if (!dd) return 0;
    uint16_t cnt = 0;
    struct lfs_info info;
    while (flash_lfs_readdir(dd, (char*)&info) > 0) {
        if (info.type == LFS_TYPE_REG && strcmp(info.name, "_seq") != 0) cnt++;
    }
    flash_lfs_closedir(dd);
    return cnt;
}

void backlog_drop_oldest(backlog_channel_t ch) {
    char name[LFS_NAME_MAX + 1];
    if (!find_oldest_name(ch, name, sizeof(name))) return;
    char path[320]; snprintf(path, sizeof(path), "/pc_backlog/%s/%s", CHNAME[ch], name);
    flash_lfs_remove(path);
}
