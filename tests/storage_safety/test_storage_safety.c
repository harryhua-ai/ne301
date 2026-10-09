/*
 * test_storage_safety.c — host error-injection regression for issue #37.
 *
 * Proves, with the REAL littlefs, the REAL NVS library and the REAL decision
 * modules from the firmware tree (Custom/Hal/storage_safety.c,
 * Custom/Core/System/json_config_boot_policy.c, Custom/Common/Lib/nvs/nvs.c):
 *
 *   AC1  mount failure never formats/erases; media preserved byte-for-byte
 *   AC2  NVS init failure preserves media and reports not-ready; config
 *        policy refuses persistence on degraded/unrecognized states
 *   AC3  only a proven-blank medium is classified NEEDS_INIT / FIRST_BLANK;
 *        explicit format returns the real result
 *   AC4  this file (isolated host injection); target evidence = make app
 *
 * Normal-path compatibility is asserted too: a healthy volume keeps its
 * files across probe/remount, and a genuinely blank NVS still boots to the
 * first-boot state.
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "lfs.h"
#include "nvs.h"
#include "fake_bd.h"
#include "storage_safety.h"
#include "json_config_boot_policy.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        g_failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
    } \
} while (0)

#define BD_BLOCKS 64
#define BD_BLOCK_SIZE 4096

static uint8_t bd_mem[BD_BLOCKS * BD_BLOCK_SIZE] __attribute__((aligned(8)));
static uint8_t snapshot[sizeof(bd_mem)];

static void snapshot_take(fake_bd_t *bd)
{
    memcpy(snapshot, bd->mem, sizeof(bd_mem));
}
static void snapshot_diff_is_zero(fake_bd_t *bd)
{
    g_checks++;
    if (memcmp(snapshot, bd->mem, sizeof(bd_mem)) != 0) {
        g_failures++;
        fprintf(stderr, "FAIL %s:%d: media changed after a refuse-path operation\n",
                __func__, __LINE__);
    }
}

/* Write one small file through plain littlefs (setup helper).
 * Creates the parent directory if the path nests (e.g. "/data/f.txt"). */
static void write_file(struct lfs_config *cfg, const char *path, const char *text)
{
    lfs_t lfs;
    CHECK(lfs_format(&lfs, cfg) == LFS_ERR_OK);
    CHECK(lfs_mount(&lfs, cfg) == LFS_ERR_OK);
    const char *slash = strchr(path + 1, '/');
    if (slash) {
        char dir[64];
        size_t dlen = (size_t)(slash - path);
        if (dlen < sizeof(dir)) {
            memcpy(dir, path, dlen);
            dir[dlen] = '\0';
            CHECK(lfs_mkdir(&lfs, dir) == LFS_ERR_OK);
        }
    }
    lfs_file_t f;
    CHECK(lfs_file_open(&lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == LFS_ERR_OK);
    CHECK(lfs_file_write(&lfs, &f, text, strlen(text)) == (lfs_ssize_t)strlen(text));
    CHECK(lfs_file_close(&lfs, &f) == LFS_ERR_OK);
    CHECK(lfs_unmount(&lfs) == LFS_ERR_OK);
}

/* ==================== AC1/AC3: littlefs probe/format policy ==================== */

static void test_blank_volume_is_needs_init_and_untouched(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    fake_bd_reset_counts(&bd);
    snapshot_take(&bd);

    lfs_t lfs;
    storage_lfs_state_t st = storage_lfs_probe_and_mount(&lfs, &cfg);

    CHECK(st == STORAGE_LFS_STATE_NEEDS_INIT);
    CHECK(bd.erase_count == 0);   /* AC1: no erase on the refusal path */
    CHECK(bd.prog_count == 0);    /* AC1: no write on the refusal path */
    snapshot_diff_is_zero(&bd);   /* AC1: media preserved byte-for-byte */
}

static void test_corrupted_volume_is_unavailable_and_untouched(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    /* Existing (then corrupted) content: non-blank + broken superblock. */
    write_file(&cfg, "/keep.txt", "precious user data");
    fake_bd_corrupt_block(&bd, 0, 0x00);
    fake_bd_corrupt_block(&bd, 1, 0xA5);
    fake_bd_reset_counts(&bd);
    snapshot_take(&bd);

    lfs_t lfs;
    storage_lfs_state_t st = storage_lfs_probe_and_mount(&lfs, &cfg);

    CHECK(st == STORAGE_LFS_STATE_UNAVAILABLE); /* not NEEDS_INIT: not blank */
    CHECK(bd.erase_count == 0);
    CHECK(bd.prog_count == 0);
    snapshot_diff_is_zero(&bd);
}

static void test_read_failure_is_unavailable_never_blank(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    /* IO failure from the very first read: medium unreadable. */
    bd.fail_reads_from = 0;
    fake_bd_reset_counts(&bd);
    snapshot_take(&bd);

    lfs_t lfs;
    storage_lfs_state_t st = storage_lfs_probe_and_mount(&lfs, &cfg);

    CHECK(st == STORAGE_LFS_STATE_UNAVAILABLE); /* unreadable is never "blank" */
    CHECK(bd.erase_count == 0);
    CHECK(bd.prog_count == 0);
    snapshot_diff_is_zero(&bd);
}

static void test_healthy_volume_mounts_and_keeps_files(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    write_file(&cfg, "/data/keep.txt", "normal-path-content");

    lfs_t lfs;
    storage_lfs_state_t st = storage_lfs_probe_and_mount(&lfs, &cfg);
    CHECK(st == STORAGE_LFS_STATE_OK);

    /* Normal path compatibility: file still readable after probe/mount. */
    lfs_file_t f;
    char buf[32] = {0};
    CHECK(lfs_file_open(&lfs, &f, "/data/keep.txt", LFS_O_RDONLY) == LFS_ERR_OK);
    CHECK(lfs_file_read(&lfs, &f, buf, sizeof(buf)) > 0);
    CHECK(lfs_file_close(&lfs, &f) == LFS_ERR_OK);
    CHECK(strcmp(buf, "normal-path-content") == 0);
    CHECK(lfs_unmount(&lfs) == LFS_ERR_OK);
}

static void test_explicit_format_on_blank_volume_succeeds(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);

    lfs_t lfs;
    CHECK(storage_lfs_probe_and_mount(&lfs, &cfg) == STORAGE_LFS_STATE_NEEDS_INIT);

    /* The ONLY authorized init path: explicit format. Must report truth. */
    bool mounted = false;
    CHECK(storage_lfs_format_volume(&lfs, &cfg, &mounted) == LFS_ERR_OK);
    CHECK(mounted == true);

    lfs_file_t f;
    CHECK(lfs_file_open(&lfs, &f, "/first.txt", LFS_O_WRONLY | LFS_O_CREAT) == LFS_ERR_OK);
    CHECK(lfs_file_close(&lfs, &f) == LFS_ERR_OK);
    CHECK(lfs_unmount(&lfs) == LFS_ERR_OK);
}

static void test_explicit_format_on_healthy_volume_destroys_files_and_reports(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    write_file(&cfg, "/old.txt", "to-be-erased-by-explicit-format");

    lfs_t lfs;
    CHECK(storage_lfs_probe_and_mount(&lfs, &cfg) == STORAGE_LFS_STATE_OK);
    bool mounted = true;
    CHECK(storage_lfs_format_volume(&lfs, &cfg, &mounted) == LFS_ERR_OK);
    CHECK(mounted == true);

    struct lfs_info info;
    CHECK(lfs_stat(&lfs, "/old.txt", &info) == LFS_ERR_NOENT); /* really gone */
    CHECK(lfs_unmount(&lfs) == LFS_ERR_OK);
}

static void test_format_failure_is_reported_not_faked(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    write_file(&cfg, "/x.txt", "x");

    lfs_t lfs;
    CHECK(storage_lfs_probe_and_mount(&lfs, &cfg) == STORAGE_LFS_STATE_OK);

    /* Inject prog failures: format cannot succeed; result must say so. */
    fake_bd_reset_counts(&bd);
    snapshot_take(&bd);
    bd.fail_progs_from = 0;
    bool mounted = true;
    int rc = storage_lfs_format_volume(&lfs, &cfg, &mounted);
    CHECK(rc != LFS_ERR_OK);          /* AC3: real result, never fake success */
    CHECK(mounted == false);          /* volume left unmounted, not "mounted-ish" */

    /* Review Blocker 3: a FAILED format may already have partially erased
     * the medium. The test encodes that reality so no caller can honestly
     * answer "storage left unchanged" / "media preserved": after this
     * failure the medium is observably modified and the volume does not
     * mount. */
    CHECK(bd.erase_count > 0);        /* erases were issued before the failure */
    g_checks++;
    if (memcmp(snapshot, bd.mem, sizeof(bd_mem)) == 0) {
        g_failures++;
        fprintf(stderr, "FAIL %s:%d: expected failed format to possibly modify media\n",
                __func__, __LINE__);
    }
}

/* ==================== blank-check primitives ==================== */

static void test_blank_check_range_matrix(void)
{
    bool blank = false;

    CHECK(storage_blank_check_range(fake_bd_raw_read, 0, 0, &blank) != 0); /* len 0 refused */
    CHECK(storage_blank_check_range(NULL, 0, 10, &blank) != 0);
    CHECK(storage_blank_check_range(fake_bd_raw_read, 0, 10, NULL) != 0);

    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    fake_bd_reset_counts(&bd);

    CHECK(storage_blank_check_range(fake_bd_raw_read, 0, sizeof(bd_mem), &blank) == 0);
    CHECK(blank == true);

    bd_mem[12345] = 0x5A; /* single flipped byte anywhere => not blank */
    CHECK(storage_blank_check_range(fake_bd_raw_read, 0, sizeof(bd_mem), &blank) == 0);
    CHECK(blank == false);

    CHECK(storage_blank_check_range(fake_bd_raw_read, 0, 100, &blank) == 0);
    CHECK(blank == true); /* the flipped byte is outside this range */

    /* Injected read failure: unreadable range is never reported blank. */
    bd.fail_reads_from = 0;
    fake_bd_reset_counts(&bd);
    CHECK(storage_blank_check_range(fake_bd_raw_read, 0, 100, &blank) != 0);
    CHECK(blank == false);
}

static void test_lfs_volume_blank_direct(void)
{
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    bool blank = false;

    CHECK(storage_lfs_volume_blank(&cfg, &blank) == 0);
    CHECK(blank == true);

    bd.mem[BD_BLOCK_SIZE * 3 + 17] = 0x00;
    CHECK(storage_lfs_volume_blank(&cfg, &blank) == 0);
    CHECK(blank == false);

    bd.fail_reads_from = 0;
    fake_bd_reset_counts(&bd);
    CHECK(storage_lfs_volume_blank(&cfg, &blank) != 0);
    CHECK(blank == false);

    CHECK(storage_lfs_volume_blank(NULL, &blank) != 0);
}

/* ==================== AC2: config boot policy matrix ==================== */

#define MAGIC 0x41494341U

static void test_boot_policy_matrix(void)
{
    /* Backend down -> refuse, regardless of any other evidence. */
    CHECK(json_config_boot_classify(false, 0, true, 0, MAGIC, MAGIC)
          == JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE);

    /* Backend up but even the blank probe failed -> refuse. */
    CHECK(json_config_boot_classify(true, -2, false, -1, 0, MAGIC)
          == JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE);

    /* Valid magic -> persisted config, normal path. */
    CHECK(json_config_boot_classify(true, 0, false, 0, MAGIC, MAGIC)
          == JSON_CONFIG_BOOT_PERSISTED);

    /* Missing magic + PROVEN blank -> PENDING_INIT (review Blocker 2: a
     * blank device awaits an AUTHORIZED first initialization; it is no
     * longer auto-initialized at boot). */
    CHECK(json_config_boot_classify(true, 0, true, -1, 0, MAGIC)
          == JSON_CONFIG_BOOT_PENDING_INIT);

    /* Missing magic + NOT blank -> unrecognized old data; refuse. */
    CHECK(json_config_boot_classify(true, 0, false, -1, 0, MAGIC)
          == JSON_CONFIG_BOOT_UNRECOGNIZED);

    /* Garbage magic + NOT blank -> refuse (old data, unknown layout). */
    CHECK(json_config_boot_classify(true, 0, false, 0, 0xDEADBEEFU, MAGIC)
          == JSON_CONFIG_BOOT_UNRECOGNIZED);

    /* Garbage magic + proven blank -> PENDING_INIT. */
    CHECK(json_config_boot_classify(true, 0, true, 0, 0xDEADBEEFU, MAGIC)
          == JSON_CONFIG_BOOT_PENDING_INIT);

    /* Policy: persistence ONLY on PERSISTED. Review Blocker 2 (negative):
     * a proven-blank PENDING_INIT device must NOT be written automatically
     * at boot — proven blank is not a write authorization. */
    CHECK(json_config_boot_allow_persist(JSON_CONFIG_BOOT_PERSISTED) == true);
    CHECK(json_config_boot_allow_persist(JSON_CONFIG_BOOT_PENDING_INIT) == false);
    CHECK(json_config_boot_allow_persist(JSON_CONFIG_BOOT_UNRECOGNIZED) == false);
    CHECK(json_config_boot_allow_persist(JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE) == false);

    /* Review Blocker 1 (negative): corrupted/unknown credential source must
     * NOT allow admin auth (the RAM default must never become a working
     * admin credential). Proven blank keeps the factory bootstrap
     * credential valid; persisted config keeps the real credential. */
    CHECK(json_config_boot_allow_admin_auth(JSON_CONFIG_BOOT_PERSISTED) == true);
    CHECK(json_config_boot_allow_admin_auth(JSON_CONFIG_BOOT_PENDING_INIT) == true);
    CHECK(json_config_boot_allow_admin_auth(JSON_CONFIG_BOOT_UNRECOGNIZED) == false);
    CHECK(json_config_boot_allow_admin_auth(JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE) == false);

    /* State names exist (log-facing contract). */
    CHECK(json_config_boot_state_name(JSON_CONFIG_BOOT_PERSISTED) != 0);
    CHECK(json_config_boot_state_name(JSON_CONFIG_BOOT_PENDING_INIT) != 0);
    CHECK(json_config_boot_state_name(JSON_CONFIG_BOOT_UNRECOGNIZED) != 0);
    CHECK(storage_lfs_state_name(STORAGE_LFS_STATE_NEEDS_INIT) != 0);
    CHECK(storage_lfs_state_name(STORAGE_LFS_STATE_UNAVAILABLE) != 0);
}

/* ==================== AC4 gap (a): corrupted credential chain ====================
 * NVS corrupted/unreadable -> RAM default password present -> sensitive
 * admin operations MUST be refused. Encodes the review Blocker 1 decision
 * chain end-to-end at the policy layer (the auth_mgr wiring consumes exactly
 * this decision via json_config_mgr_credentials_trusted()). */
static void test_corrupted_credential_source_chain(void)
{
    /* Chain 1: backend unreadable (read failure injection). */
    json_config_boot_state_t st =
        json_config_boot_classify(false, -1, false, -1, 0, MAGIC);
    CHECK(st == JSON_CONFIG_BOOT_BACKEND_UNAVAILABLE);
    CHECK(json_config_boot_allow_admin_auth(st) == false);   /* auth refused */
    CHECK(json_config_boot_allow_persist(st) == false);      /* writes refused */

    /* Chain 2: readable but unrecognized data (magic missing, not blank). */
    st = json_config_boot_classify(true, 0, false, -1, 0, MAGIC);
    CHECK(st == JSON_CONFIG_BOOT_UNRECOGNIZED);
    CHECK(json_config_boot_allow_admin_auth(st) == false);   /* auth refused */
    CHECK(json_config_boot_allow_persist(st) == false);      /* writes refused */

    /* The only states where admin auth stays allowed: verified stored
     * credential, and PROVEN-BLANK bootstrap (factory default is the real
     * credential of an empty device). */
    CHECK(json_config_boot_allow_admin_auth(JSON_CONFIG_BOOT_PERSISTED) == true);
    CHECK(json_config_boot_allow_admin_auth(JSON_CONFIG_BOOT_PENDING_INIT) == true);
}

/* ==================== AC4 gap (b): unauthorized first write ====================
 * A proven-blank device must NOT be auto-initialized at boot: no automatic
 * persistent write may occur; the device stays PENDING_INIT until the
 * explicit authorized entry (factory reset). Review Blocker 2 negative. */
static void test_unauthorized_first_write_chain(void)
{
    json_config_boot_state_t st =
        json_config_boot_classify(true, 0, true, -1, 0, MAGIC); /* proven blank */
    CHECK(st == JSON_CONFIG_BOOT_PENDING_INIT);

    /* The decision the firmware load path enforces: persist refused -> the
     * boot-time default write cannot happen (json_config_load_from_nvs
     * returns before ANY write; json_config_save_to_nvs refuses). */
    CHECK(json_config_boot_allow_persist(st) == false);

    /* Bootstrap auth stays available so an operator can reach the explicit
     * authorized init entry on a genuinely blank device. */
    CHECK(json_config_boot_allow_admin_auth(st) == true);
}

/* ==================== AC2: real NVS library over injected flash ==================== */

#define NVS_SECTORS 4
#define NVS_SECTOR_SIZE 4096
static uint8_t nvs_mem[NVS_SECTORS * NVS_SECTOR_SIZE];

static int nvs_fake_read(uint32_t offset, void *data, size_t len)
{
    extern fake_bd_t *g_raw_bd;
    if (!g_raw_bd) return -1;
    g_raw_bd->read_count++;
    if (g_raw_bd->fail_reads_from >= 0 &&
        g_raw_bd->read_count - 1 >= g_raw_bd->fail_reads_from) {
        g_raw_bd->read_errors++;
        return -1;
    }
    if ((size_t)offset + len > sizeof(nvs_mem)) return -1;
    memcpy(data, nvs_mem + offset, len);
    return 0;
}
static int nvs_fake_write(uint32_t offset, void *data, size_t len)
{
    extern fake_bd_t *g_raw_bd;
    if (!g_raw_bd) return -1;
    g_raw_bd->prog_count++;
    if ((size_t)offset + len > sizeof(nvs_mem)) return -1;
    memcpy(nvs_mem + offset, data, len);
    return 0;
}
static int nvs_fake_erase(uint32_t offset, size_t size)
{
    extern fake_bd_t *g_raw_bd;
    if (!g_raw_bd) return -1;
    g_raw_bd->erase_count++;
    if ((size_t)offset % NVS_SECTOR_SIZE != 0) return -1;
    memset(nvs_mem + offset, 0xFF, size);
    return 0;
}

static void nvs_setup(void)
{
    memset(nvs_mem, 0xFF, sizeof(nvs_mem));
}

static void nvs_noop_lock(void *m) { (void)m; }
static void nvs_noop_unlock(void *m) { (void)m; }

static void nvs_mount(nvs_fs_t *fs)
{
    memset(fs, 0, sizeof(*fs));
    fs->offset = 0;
    fs->sector_size = NVS_SECTOR_SIZE;
    fs->sector_count = NVS_SECTORS;
    fs->flash_parameters.write_block_size = 4;
    fs->flash_parameters.erase_value = 0xFF;
    fs->flash_ops.flash_read = nvs_fake_read;
    fs->flash_ops.flash_write = nvs_fake_write;
    fs->flash_ops.flash_erase = nvs_fake_erase;
    fs->flash_ops.flash_write_protection_set = NULL;
    /* Mirrors storage.c's storage_nvs_init wiring (host: no-op mutex). */
    fs->mutex_ops.lock = nvs_noop_lock;
    fs->mutex_ops.unlock = nvs_noop_unlock;
    fs->mutex = NULL;
}

static void test_nvs_blank_init_writes_no_erase_and_roundtrips(void)
{
    nvs_setup();
    /* Fresh counter carrier for the injection hooks. */
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    fake_bd_reset_counts(&bd);

    nvs_fs_t fs;
    nvs_mount(&fs);
    CHECK(nvs_init(&fs) == 0);
    CHECK(fs.ready == true);
    /* Genuine first boot on blank media: initialization must not need to
     * erase anything (writes are append-only into erased space). */
    CHECK(bd.erase_count == 0);

    const char *msg = "hello-nvs";
    CHECK((int)nvs_write(&fs, "k1", msg, strlen(msg) + 1) >= 0);
    char buf[32] = {0};
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) >= 0);
    CHECK(strcmp(buf, msg) == 0);

    /* Legacy in-place default backfill scenario: rewrite same key. */
    const char *msg2 = "updated";
    CHECK((int)nvs_write(&fs, "k1", msg2, strlen(msg2) + 1) >= 0);
    memset(buf, 0, sizeof(buf));
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) >= 0);
    CHECK(strcmp(buf, msg2) == 0);

    CHECK(nvs_delete(&fs, "k1") >= 0);
    CHECK((int)nvs_read(&fs, "k1", buf, sizeof(buf)) == -ENOENT);
}

static void test_nvs_read_failure_init_refuses_and_preserves(void)
{
    nvs_setup();
    fake_bd_t bd; struct lfs_config cfg;
    fake_bd_init(&bd, bd_mem, BD_BLOCK_SIZE, BD_BLOCKS);
    fake_bd_make_config(&bd, &cfg);
    fake_bd_reset_counts(&bd);

    /* Put data in so "preserved" is observable. */
    nvs_fs_t fs;
    nvs_mount(&fs);
    CHECK(nvs_init(&fs) == 0);
    const char *val = "admin-password-hash";
    CHECK((int)nvs_write(&fs, "cred", val, strlen(val) + 1) >= 0);

    /* Now make ALL reads fail (hardware fault injection) and re-init. */
    extern fake_bd_t *g_raw_bd;
    g_raw_bd->fail_reads_from = 0;
    fake_bd_reset_counts(&bd);
    uint8_t before[sizeof(nvs_mem)];
    memcpy(before, nvs_mem, sizeof(nvs_mem));

    nvs_fs_t fs2;
    nvs_mount(&fs2);
    int rc = nvs_init(&fs2);
    CHECK(rc != 0);                 /* refuses to come up */
    CHECK(fs2.ready == false);      /* and reports not-ready */

    /* AC2: init failure must not erase or rewrite the partition. */
    CHECK(bd.erase_count == 0);
    CHECK(bd.prog_count == 0);
    CHECK(memcmp(before, nvs_mem, sizeof(nvs_mem)) == 0);

    /* Accessors against a not-ready instance fail closed (this is the exact
     * error the storage facade and config layer key off). */
    char buf[8];
    CHECK((int)nvs_read(&fs2, "cred", buf, sizeof(buf)) == -EACCES);
    CHECK((int)nvs_write(&fs2, "cred", "x", 2) == -EACCES);
    CHECK(nvs_clear(&fs2) == -EACCES);
    CHECK(nvs_delete(&fs2, "cred") == -EACCES);

    g_raw_bd->fail_reads_from = -1; /* leave injection off for later tests */
}

int main(void)
{
    test_blank_volume_is_needs_init_and_untouched();
    test_corrupted_volume_is_unavailable_and_untouched();
    test_read_failure_is_unavailable_never_blank();
    test_healthy_volume_mounts_and_keeps_files();
    test_explicit_format_on_blank_volume_succeeds();
    test_explicit_format_on_healthy_volume_destroys_files_and_reports();
    test_format_failure_is_reported_not_faked();
    test_blank_check_range_matrix();
    test_lfs_volume_blank_direct();
    test_boot_policy_matrix();
    test_corrupted_credential_source_chain();
    test_unauthorized_first_write_chain();
    test_nvs_blank_init_writes_no_erase_and_roundtrips();
    test_nvs_read_failure_init_refuses_and_preserves();

    printf("\nstorage_safety host tests: %d checks, %d failures\n",
           g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
