#include <string.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

#include "app_sidecar_verify.h"

#define SC_STATE_HEADER 0
#define SC_STATE_MANIFEST 1
#define SC_STATE_PAYLOAD 2
#define SC_STATE_SIGNATURE 3
#define SC_STATE_FINISHED 4

static const uint8_t SC_MAGIC[APP_SIDECAR_MAGIC_SIZE] = {
    0x4Eu, 0x45u, 0x55u, 0x49u, 0x01u, 0x00u, 0x00u, 0x00u
};

static const uint8_t SC_SPKI_PREFIX[26u] = {
    0x30u, 0x59u, 0x30u, 0x13u, 0x06u, 0x07u, 0x2Au, 0x86u, 0x48u, 0xCEu,
    0x3Du, 0x02u, 0x01u, 0x06u, 0x08u, 0x2Au, 0x86u, 0x48u, 0xCEu, 0x3Du,
    0x03u, 0x01u, 0x07u, 0x03u, 0x42u, 0x00u
};

static const uint32_t SC_SHA_K[64u] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static uint32_t sc_ror32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static uint32_t sc_sha_load32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void sc_sha_store32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void sc_sha_compress(app_sidecar_sha256_t *s, const uint8_t block[64u])
{
    uint32_t w[64u];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t t1;
    uint32_t t2;
    size_t i;

    for (i = 0u; i < 16u; i++) {
        w[i] = sc_sha_load32(block + (i * 4u));
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = sc_ror32(w[i - 15u], 7u) ^ sc_ror32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        uint32_t s1 = sc_ror32(w[i - 2u], 17u) ^ sc_ror32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    a = s->state[0];
    b = s->state[1];
    c = s->state[2];
    d = s->state[3];
    e = s->state[4];
    f = s->state[5];
    g = s->state[6];
    h = s->state[7];

    for (i = 0u; i < 64u; i++) {
        uint32_t s1 = sc_ror32(e, 6u) ^ sc_ror32(e, 11u) ^ sc_ror32(e, 25u);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t s0 = sc_ror32(a, 2u) ^ sc_ror32(a, 13u) ^ sc_ror32(a, 22u);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + s1 + ch + SC_SHA_K[i] + w[i];
        t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

static void sc_sha_init(app_sidecar_sha256_t *s)
{
    s->state[0] = 0x6A09E667u;
    s->state[1] = 0xBB67AE85u;
    s->state[2] = 0x3C6EF372u;
    s->state[3] = 0xA54FF53Au;
    s->state[4] = 0x510E527Fu;
    s->state[5] = 0x9B05688Cu;
    s->state[6] = 0x1F83D9ABu;
    s->state[7] = 0x5BE0CD19u;
    s->total = 0u;
    s->buf_len = 0u;
}

static void sc_sha_update(app_sidecar_sha256_t *s, const uint8_t *data, size_t len)
{
    size_t pos = 0u;

    s->total += (uint64_t)len;
    while (len > 0u) {
        size_t take = 64u - s->buf_len;
        if (take > len) {
            take = len;
        }
        (void)memcpy(&s->buf[s->buf_len], data + pos, take);
        s->buf_len += take;
        pos += take;
        len -= take;
        if (s->buf_len == 64u) {
            sc_sha_compress(s, s->buf);
            s->buf_len = 0u;
        }
    }
}

static void sc_sha_final(app_sidecar_sha256_t *s, uint8_t out[APP_SIDECAR_SHA256_LEN])
{
    uint8_t tail[72u];
    uint64_t bits = s->total * 8u;
    size_t pad_len;
    size_t i;

    (void)memset(tail, 0, sizeof(tail));
    tail[0] = 0x80u;
    pad_len = (s->buf_len < 56u) ? (56u - s->buf_len) : (120u - s->buf_len);
    sc_sha_store32(&tail[pad_len], (uint32_t)(bits >> 32));
    sc_sha_store32(&tail[pad_len + 4u], (uint32_t)bits);
    sc_sha_update(s, tail, pad_len + 8u);

    for (i = 0u; i < 8u; i++) {
        sc_sha_store32(&out[i * 4u], s->state[i]);
    }
}

static uint16_t sc_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t sc_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t sc_fold(uint8_t c)
{
    if ((c >= (uint8_t)'a') && (c <= (uint8_t)'z')) {
        return (uint8_t)(c - 32u);
    }
    return c;
}

static int sc_path_segment_valid(const uint8_t *s, size_t len)
{
    size_t i;

    if (len == 0u) {
        return 0;
    }
    if (!((s[0] >= (uint8_t)'A' && s[0] <= (uint8_t)'Z') ||
          (s[0] >= (uint8_t)'a' && s[0] <= (uint8_t)'z') ||
          (s[0] >= (uint8_t)'0' && s[0] <= (uint8_t)'9'))) {
        return 0;
    }
    for (i = 1u; i < len; i++) {
        uint8_t c = s[i];
        if (!((c >= (uint8_t)'A' && c <= (uint8_t)'Z') ||
              (c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              (c == (uint8_t)'.' || c == (uint8_t)'_' || c == (uint8_t)'-'))) {
            return 0;
        }
    }
    return 1;
}

static int sc_path_valid(const uint8_t *path, size_t len)
{
    size_t seg_start = 0u;
    size_t i;

    if ((len < 1u) || (len > APP_SIDECAR_PATH_MAX)) {
        return 0;
    }
    if ((path[0] == (uint8_t)'/') || (path[len - 1u] == (uint8_t)'/')) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        uint8_t c = path[i];
        if (c == (uint8_t)'/') {
            if (i == seg_start) {
                return 0;
            }
            if (sc_path_segment_valid(path + seg_start, i - seg_start) == 0) {
                return 0;
            }
            seg_start = i + 1u;
        } else if ((c < 0x21u) || (c > 0x7Eu)) {
            return 0;
        }
    }
    if (len == seg_start) {
        return 0;
    }
    return sc_path_segment_valid(path + seg_start, len - seg_start);
}

static int sc_path_greater(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    size_t n = (alen < blen) ? alen : blen;
    size_t i;

    for (i = 0u; i < n; i++) {
        if (a[i] != b[i]) {
            return (a[i] > b[i]) ? 1 : 0;
        }
    }
    return (alen > blen) ? 1 : 0;
}

static int sc_path_fold_eq(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
    size_t i;

    if (alen != blen) {
        return 0;
    }
    for (i = 0u; i < alen; i++) {
        if (sc_fold(a[i]) != sc_fold(b[i])) {
            return 0;
        }
    }
    return 1;
}

static int sc_identifier_valid(const uint8_t *field, size_t slot_len)
{
    size_t text_len = 0u;
    size_t i;

    if (slot_len == 0u) {
        return 0;
    }
    while ((text_len < slot_len) && (field[text_len] != 0x00u)) {
        text_len++;
    }
    for (i = text_len; i < slot_len; i++) {
        if (field[i] != 0x00u) {
            return 0;
        }
    }
    if (text_len == 0u) {
        return 0;
    }
    if ((field[0] < (uint8_t)'a') || (field[0] > (uint8_t)'z')) {
        return 0;
    }
    for (i = 0u; i < text_len; i++) {
        uint8_t c = field[i];
        if (!((c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
              (c == (uint8_t)'-'))) {
            return 0;
        }
    }
    if (text_len > 1u) {
        if (field[text_len - 1u] == (uint8_t)'-') {
            return 0;
        }
    }
    return 1;
}

static int sc_slot_equals(const uint8_t *slot, size_t slot_len, const char *str, size_t str_len)
{
    size_t i;

    if ((str == NULL) || (str_len == 0u) || (str_len > slot_len)) {
        return 0;
    }
    if (memcmp(slot, str, str_len) != 0) {
        return 0;
    }
    for (i = str_len; i < slot_len; i++) {
        if (slot[i] != 0x00u) {
            return 0;
        }
    }
    return 1;
}

static int sc_validate_spki(const uint8_t *der, size_t len, uint8_t point_out[65u])
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    int rc = -1;

    if (len != APP_SIDECAR_SPKI_P256_LEN) {
        return -1;
    }
    if (memcmp(der, SC_SPKI_PREFIX, sizeof(SC_SPKI_PREFIX)) != 0) {
        return -1;
    }

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
        goto cleanup;
    }
    if (mbedtls_ecp_point_read_binary(&grp, &q, der + 26u, 65u) != 0) {
        goto cleanup;
    }
    if (mbedtls_ecp_check_pubkey(&grp, &q) != 0) {
        goto cleanup;
    }
    (void)memcpy(point_out, der + 26u, 65u);
    rc = 0;

cleanup:
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return rc;
}

static int sc_der_uint(const uint8_t *sig, size_t limit, size_t *pos, mbedtls_mpi *out)
{
    size_t start = *pos;
    size_t int_len;

    if (start >= limit) {
        return -1;
    }
    if (sig[start] != 0x02u) {
        return -1;
    }
    start++;
    if (start >= limit) {
        return -1;
    }
    int_len = sig[start];
    if ((int_len >= 0x80u) || (int_len == 0u) || (int_len > 33u)) {
        return -1;
    }
    start++;
    if ((start + int_len) > limit) {
        return -1;
    }
    if ((sig[start] & 0x80u) != 0u) {
        return -1;
    }
    if (sig[start] == 0x00u) {
        if ((int_len < 2u) || (sig[start + 1u] < 0x80u)) {
            return -1;
        }
    }
    if (mbedtls_mpi_read_binary(out, sig + start, int_len) != 0) {
        return -1;
    }
    *pos = start + int_len;
    return 0;
}

static int sc_der_parse(const uint8_t *sig, size_t sig_len, mbedtls_mpi *r, mbedtls_mpi *s)
{
    size_t seq_len;
    size_t pos = 2u;

    if ((sig_len < APP_SIDECAR_DER_LEN_MIN) || (sig_len > APP_SIDECAR_DER_LEN_MAX)) {
        return -1;
    }
    if (sig[0] != 0x30u) {
        return -1;
    }
    if (sig[1] >= 0x80u) {
        return -1;
    }
    seq_len = sig[1];
    if (((size_t)seq_len + 2u) != sig_len) {
        return -1;
    }
    if (sc_der_uint(sig, sig_len, &pos, r) != 0) {
        return -1;
    }
    if (sc_der_uint(sig, sig_len, &pos, s) != 0) {
        return -1;
    }
    if (pos != sig_len) {
        return -1;
    }
    return 0;
}

static app_sidecar_result_t sc_header_process(app_sidecar_verifier_t *v)
{
    const uint8_t *h = v->header;

    if (memcmp(h, SC_MAGIC, APP_SIDECAR_MAGIC_SIZE) != 0) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    v->manifest_len = sc_rd32(h + 8u);
    v->asset_payload_len = sc_rd32(h + 12u);
    if ((v->manifest_len < (APP_SIDECAR_MANIFEST_FIXED_SIZE + APP_SIDECAR_RECORD_FIXED_SIZE)) ||
        (v->manifest_len > APP_SIDECAR_MANIFEST_LEN_MAX)) {
        return APP_SIDECAR_ERR_LIMIT;
    }
    if ((v->asset_payload_len < 1u) || (v->asset_payload_len > APP_SIDECAR_ASSET_PAYLOAD_MAX)) {
        return APP_SIDECAR_ERR_LIMIT;
    }
    v->manifest_have = 0u;
    v->state = SC_STATE_MANIFEST;
    return APP_SIDECAR_OK;
}

static app_sidecar_result_t sc_manifest_process(app_sidecar_verifier_t *v)
{
    const uint8_t *m = v->manifest;
    app_sidecar_sha256_t key_sha;
    uint8_t key_digest[APP_SIDECAR_SHA256_LEN];
    uint16_t version_major;
    uint32_t host_ui_abi;
    uint32_t reserved;
    uint64_t total_len = 0u;
    size_t off = APP_SIDECAR_MANIFEST_FIXED_SIZE;
    uint16_t idx = 0u;
    app_sidecar_result_t rc = APP_SIDECAR_OK;

    if (sc_identifier_valid(m + 0u, APP_SIDECAR_PUBLISHER_ID_SLOT) == 0) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    if (sc_identifier_valid(m + APP_SIDECAR_PUBLISHER_ID_SLOT, APP_SIDECAR_APP_ID_SLOT) == 0) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    if (sc_slot_equals(m + 0u, APP_SIDECAR_PUBLISHER_ID_SLOT,
                       v->trust.publisher_id, v->trust.publisher_id_len) == 0) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }
    if (sc_slot_equals(m + APP_SIDECAR_PUBLISHER_ID_SLOT, APP_SIDECAR_APP_ID_SLOT,
                       v->trust.app_id, v->trust.app_id_len) == 0) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }

    sc_sha_init(&key_sha);
    sc_sha_update(&key_sha, v->trust.spki_der, v->trust.spki_der_len);
    sc_sha_final(&key_sha, key_digest);
    if (memcmp(m + 48u, key_digest, APP_SIDECAR_SHA256_LEN) != 0) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }
    if (memcmp(m + 80u, v->trust.native_tbs_sha256, APP_SIDECAR_SHA256_LEN) != 0) {
        return APP_SIDECAR_ERR_IDENTITY_MISMATCH;
    }

    version_major = sc_rd16(m + 112u);
    if (version_major < APP_SIDECAR_VERSION_MAJOR_MIN) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    v->file_count = sc_rd16(m + 118u);
    if ((v->file_count < 1u) || (v->file_count > APP_SIDECAR_FILE_COUNT_MAX)) {
        return APP_SIDECAR_ERR_LIMIT;
    }
    host_ui_abi = sc_rd32(m + 120u);
    if (host_ui_abi != APP_SIDECAR_HOST_UI_ABI) {
        return APP_SIDECAR_ERR_ABI_INCOMPATIBLE;
    }
    reserved = sc_rd32(m + 124u);
    if (reserved != 0u) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }

    while (off < (size_t)v->manifest_len) {
        const uint8_t *rec = m + off;
        uint16_t path_len;
        uint32_t file_len;
        size_t rec_len;
        const uint8_t *path;
        uint16_t j;

        if (idx >= v->file_count) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        if (((size_t)v->manifest_len - off) < APP_SIDECAR_RECORD_FIXED_SIZE) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        path_len = sc_rd16(rec + 0u);
        file_len = sc_rd32(rec + 2u);
        if ((path_len < 1u) || (path_len > APP_SIDECAR_PATH_MAX)) {
            return APP_SIDECAR_ERR_LIMIT;
        }
        if ((file_len < 1u) || (file_len > APP_SIDECAR_FILE_LEN_MAX)) {
            return APP_SIDECAR_ERR_LIMIT;
        }
        rec_len = APP_SIDECAR_RECORD_FIXED_SIZE + (size_t)path_len;
        if (((size_t)off + rec_len) > (size_t)v->manifest_len) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        path = rec + APP_SIDECAR_RECORD_FIXED_SIZE;
        if (sc_path_valid(path, (size_t)path_len) == 0) {
            return APP_SIDECAR_ERR_BAD_FORMAT;
        }
        for (j = 0u; j < idx; j++) {
            if (sc_path_fold_eq(v->manifest + v->rec_path_off[j], v->rec_path_len[j],
                                path, (size_t)path_len) != 0) {
                return APP_SIDECAR_ERR_BAD_FORMAT;
            }
        }
        if (idx > 0u) {
            const uint8_t *prev = v->manifest + v->rec_path_off[idx - 1u];
            uint16_t prev_len = v->rec_path_len[idx - 1u];
            if (sc_path_greater(path, (size_t)path_len, prev, (size_t)prev_len) == 0) {
                return APP_SIDECAR_ERR_BAD_FORMAT;
            }
        }
        if ((path_len == 10u) && (memcmp(path, "index.html", 10u) == 0)) {
            v->index_html_found++;
        }
        v->rec_file_len[idx] = file_len;
        v->rec_sha_off[idx] = (uint16_t)(off + 6u);
        v->rec_path_off[idx] = (uint16_t)(off + APP_SIDECAR_RECORD_FIXED_SIZE);
        v->rec_path_len[idx] = path_len;
        total_len += (uint64_t)file_len;
        off += rec_len;
        idx++;
    }

    if (idx != v->file_count) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    if (total_len != (uint64_t)v->asset_payload_len) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    if (v->index_html_found != 1) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }

    v->file_index = 0u;
    v->file_left = v->rec_file_len[0u];
    v->payload_left = v->asset_payload_len;
    sc_sha_init(&v->file_sha);
    v->state = SC_STATE_PAYLOAD;
    return rc;
}

static app_sidecar_result_t sc_file_complete(app_sidecar_verifier_t *v)
{
    app_sidecar_sha256_t sha = v->file_sha;
    uint8_t digest[APP_SIDECAR_SHA256_LEN];
    uint16_t idx = v->file_index;

    sc_sha_final(&sha, digest);
    if (memcmp(digest, v->manifest + v->rec_sha_off[idx], APP_SIDECAR_SHA256_LEN) != 0) {
        return APP_SIDECAR_ERR_ASSET_INVALID;
    }
    v->file_index = (uint16_t)(idx + 1u);
    if (v->file_index < v->file_count) {
        v->file_left = v->rec_file_len[v->file_index];
        sc_sha_init(&v->file_sha);
    }
    return APP_SIDECAR_OK;
}

static void sc_deliver_verified_files(app_sidecar_verifier_t *v)
{
    app_sidecar_file_desc_t desc;
    uint16_t idx;

    for (idx = 0u; idx < v->file_count; idx++) {
        (void)memset(&desc, 0, sizeof(desc));
        desc.file_len = v->rec_file_len[idx];
        (void)memcpy(desc.sha256, v->manifest + v->rec_sha_off[idx], APP_SIDECAR_SHA256_LEN);
        (void)memcpy(desc.path, v->manifest + v->rec_path_off[idx], v->rec_path_len[idx]);
        desc.path[v->rec_path_len[idx]] = '\0';
        v->file_cb(v->file_user, &desc);
    }
}

app_sidecar_result_t app_sidecar_verifier_init(app_sidecar_verifier_t *v,
                                               const app_sidecar_trust_t *trust,
                                               app_sidecar_file_fn file_cb,
                                               void *user)
{
    if ((v == NULL) || (trust == NULL)) {
        return APP_SIDECAR_ERR_PARAM;
    }
    if ((trust->publisher_id == NULL) || (trust->publisher_id_len == 0u) ||
        (trust->app_id == NULL) || (trust->app_id_len == 0u) ||
        (trust->spki_der == NULL) || (trust->native_tbs_sha256 == NULL)) {
        return APP_SIDECAR_ERR_NO_TRUST;
    }
    (void)memset(v, 0, sizeof(*v));
    if (sc_validate_spki(trust->spki_der, trust->spki_der_len, v->spki_point) != 0) {
        return APP_SIDECAR_ERR_NO_TRUST;
    }
    v->trust = *trust;
    v->file_cb = file_cb;
    v->file_user = user;
    sc_sha_init(&v->tbs_sha);
    v->state = SC_STATE_HEADER;
    return APP_SIDECAR_OK;
}

app_sidecar_result_t app_sidecar_verify_consume(app_sidecar_verifier_t *v,
                                                const uint8_t *data,
                                                size_t len)
{
    app_sidecar_result_t rc = APP_SIDECAR_OK;
    size_t pos = 0u;

    if ((v == NULL) || ((data == NULL) && (len > 0u))) {
        return APP_SIDECAR_ERR_PARAM;
    }
    if (v->state < 0) {
        return (app_sidecar_result_t)v->state;
    }
    if (v->state == SC_STATE_FINISHED) {
        return APP_SIDECAR_ERR_PARAM;
    }
    while (pos < len) {
        if (v->state == SC_STATE_HEADER) {
            size_t take = APP_SIDECAR_HEADER_SIZE - v->header_have;
            if (take > (len - pos)) {
                take = len - pos;
            }
            (void)memcpy(&v->header[v->header_have], data + pos, take);
            sc_sha_update(&v->tbs_sha, data + pos, take);
            v->header_have += take;
            pos += take;
            if (v->header_have == APP_SIDECAR_HEADER_SIZE) {
                rc = sc_header_process(v);
                if (rc != APP_SIDECAR_OK) {
                    v->state = (int)rc;
                    return rc;
                }
            }
        } else if (v->state == SC_STATE_MANIFEST) {
            size_t take = (size_t)v->manifest_len - v->manifest_have;
            if (take > (len - pos)) {
                take = len - pos;
            }
            (void)memcpy(&v->manifest[v->manifest_have], data + pos, take);
            sc_sha_update(&v->tbs_sha, data + pos, take);
            v->manifest_have += take;
            pos += take;
            if (v->manifest_have == (size_t)v->manifest_len) {
                rc = sc_manifest_process(v);
                if (rc != APP_SIDECAR_OK) {
                    v->state = (int)rc;
                    return rc;
                }
            }
        } else if (v->state == SC_STATE_PAYLOAD) {
            size_t take = len - pos;
            if (take > (size_t)v->file_left) {
                take = (size_t)v->file_left;
            }
            sc_sha_update(&v->tbs_sha, data + pos, take);
            sc_sha_update(&v->file_sha, data + pos, take);
            pos += take;
            v->file_left -= (uint32_t)take;
            v->payload_left -= (uint32_t)take;
            if (v->file_left == 0u) {
                rc = sc_file_complete(v);
                if (rc != APP_SIDECAR_OK) {
                    v->state = (int)rc;
                    return rc;
                }
            }
            if (v->payload_left == 0u) {
                v->state = SC_STATE_SIGNATURE;
            }
        } else if (v->state == SC_STATE_SIGNATURE) {
            size_t take = APP_SIDECAR_DER_LEN_MAX - v->signature_have;
            if (take > (len - pos)) {
                take = len - pos;
            }
            if (take < (len - pos)) {
                rc = APP_SIDECAR_ERR_BAD_FORMAT;
                v->state = (int)rc;
                return rc;
            }
            (void)memcpy(&v->signature[v->signature_have], data + pos, take);
            v->signature_have += take;
            pos += take;
        } else {
            rc = APP_SIDECAR_ERR_BAD_FORMAT;
            v->state = (int)rc;
            return rc;
        }
    }
    return rc;
}

app_sidecar_result_t app_sidecar_verify_finish(app_sidecar_verifier_t *v,
                                               app_sidecar_verdict_t *out)
{
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    app_sidecar_sha256_t sha;
    uint8_t digest[APP_SIDECAR_SHA256_LEN];
    uint64_t tbs_len;
    const uint8_t *m;
    app_sidecar_result_t result = APP_SIDECAR_ERR_BAD_FORMAT;
    int cmp_rc;

    if ((v == NULL) || (out == NULL)) {
        return APP_SIDECAR_ERR_PARAM;
    }
    if (v->state < 0) {
        return (app_sidecar_result_t)v->state;
    }
    if (v->state == SC_STATE_FINISHED) {
        return APP_SIDECAR_ERR_PARAM;
    }
    if (v->state != SC_STATE_SIGNATURE) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }
    if ((v->signature_have < APP_SIDECAR_DER_LEN_MIN) ||
        (v->signature_have > APP_SIDECAR_DER_LEN_MAX)) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }

    tbs_len = (uint64_t)APP_SIDECAR_HEADER_SIZE + (uint64_t)v->manifest_len +
              (uint64_t)v->asset_payload_len;
    if (v->tbs_sha.total != tbs_len) {
        return APP_SIDECAR_ERR_BAD_FORMAT;
    }

    sha = v->tbs_sha;
    sc_sha_final(&sha, digest);

    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);

    if (sc_der_parse(v->signature, v->signature_have, &r, &s) != 0) {
        result = APP_SIDECAR_ERR_BAD_FORMAT;
        goto cleanup;
    }
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
        result = APP_SIDECAR_ERR_SIGNATURE_INVALID;
        goto cleanup;
    }
    if ((mbedtls_mpi_cmp_int(&r, 1) < 0) || (mbedtls_mpi_cmp_mpi(&r, &grp.N) >= 0) ||
        (mbedtls_mpi_cmp_int(&s, 1) < 0) || (mbedtls_mpi_cmp_mpi(&s, &grp.N) >= 0)) {
        result = APP_SIDECAR_ERR_BAD_FORMAT;
        goto cleanup;
    }
    cmp_rc = mbedtls_ecp_point_read_binary(&grp, &q, v->spki_point, 65u);
    if (cmp_rc != 0) {
        result = APP_SIDECAR_ERR_NO_TRUST;
        goto cleanup;
    }
    if (mbedtls_ecdsa_verify(&grp, digest, APP_SIDECAR_SHA256_LEN, &q, &r, &s) != 0) {
        result = APP_SIDECAR_ERR_SIGNATURE_INVALID;
        goto cleanup;
    }

    m = v->manifest;
    (void)memset(out, 0, sizeof(*out));
    (void)memcpy(out->publisher_id, v->trust.publisher_id, v->trust.publisher_id_len);
    (void)memcpy(out->app_id, v->trust.app_id, v->trust.app_id_len);
    out->version_major = sc_rd16(m + 112u);
    out->version_minor = sc_rd16(m + 114u);
    out->version_patch = sc_rd16(m + 116u);
    out->host_ui_abi = sc_rd32(m + 120u);
    out->file_count = v->file_count;
    out->manifest_len = v->manifest_len;
    out->asset_payload_len = v->asset_payload_len;
    out->signature_len = (uint8_t)v->signature_have;
    (void)memcpy(out->ui_tbs_sha256, digest, APP_SIDECAR_SHA256_LEN);
    v->state = SC_STATE_FINISHED;
    result = APP_SIDECAR_OK;

cleanup:
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    if ((result == APP_SIDECAR_OK) && (v->file_cb != NULL)) {
        sc_deliver_verified_files(v);
    }
    return result;
}

app_sidecar_result_t app_sidecar_verify_bundle(const uint8_t *data,
                                               size_t len,
                                               const app_sidecar_trust_t *trust,
                                               app_sidecar_file_fn file_cb,
                                               void *user,
                                               app_sidecar_verdict_t *out)
{
    app_sidecar_verifier_t v;
    app_sidecar_result_t rc;

    rc = app_sidecar_verifier_init(&v, trust, file_cb, user);
    if (rc != APP_SIDECAR_OK) {
        return rc;
    }
    rc = app_sidecar_verify_consume(&v, data, len);
    if (rc != APP_SIDECAR_OK) {
        return rc;
    }
    return app_sidecar_verify_finish(&v, out);
}

const char *app_sidecar_result_str(app_sidecar_result_t result)
{
    switch (result) {
    case APP_SIDECAR_OK:
        return "ok";
    case APP_SIDECAR_ERR_PARAM:
        return "param";
    case APP_SIDECAR_ERR_NO_TRUST:
        return "no_trust";
    case APP_SIDECAR_ERR_BAD_FORMAT:
        return "bad_format";
    case APP_SIDECAR_ERR_LIMIT:
        return "limit";
    case APP_SIDECAR_ERR_ABI_INCOMPATIBLE:
        return "abi_incompatible";
    case APP_SIDECAR_ERR_IDENTITY_MISMATCH:
        return "identity_mismatch";
    case APP_SIDECAR_ERR_ASSET_INVALID:
        return "asset_invalid";
    case APP_SIDECAR_ERR_SIGNATURE_INVALID:
        return "signature_invalid";
    default:
        return "unknown";
    }
}
