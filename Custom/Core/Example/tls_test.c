#include <string.h>
#include <stdio.h>
#include "Log/debug.h"
#include "tls_test.h"
#include "mbedtls/aes.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ccm.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"

int tls_test_cmd_deal(int argc, char* argv[])
{
    int ret = 0, verbose = 0;

    if (argc < 2) {
        LOG_SIMPLE("Usage: tls [func] arg...\r\n");
        return -1;
    }
    if (argc > 2) verbose = atoi(argv[2]);

    if (strcmp(argv[1], "ciphers") == 0) {
        const int *suite = mbedtls_ssl_list_ciphersuites();
        LOG_SIMPLE("Supported cipher suites:\n");
        while (*suite) {
            // Get cipher suite name
            const char *name = mbedtls_ssl_get_ciphersuite_name(*suite);
            if (name)
                LOG_SIMPLE("%s", name);
            else
                LOG_SIMPLE("Unknown suite: 0x%04x\n", *suite);
            suite++;
        }
        return 0;
    } else if (strcmp(argv[1], "aes") == 0) {
        ret = mbedtls_aes_self_test(verbose);
    } else if (strcmp(argv[1], "rsa") == 0) {
        ret = mbedtls_rsa_self_test(verbose);
    } else if (strcmp(argv[1], "ccm") == 0) {
        ret = mbedtls_ccm_self_test(verbose);
    } else if (strcmp(argv[1], "gcm") == 0) {
        ret = mbedtls_gcm_self_test(verbose);
#if defined(MBEDTLS_SHA1_C)
    } else if (strcmp(argv[1], "sha1") == 0) {
        ret = mbedtls_sha1_self_test(verbose);
#endif
    } else if (strcmp(argv[1], "sha256") == 0) {
        ret = mbedtls_sha256_self_test(verbose);
    } else {
        LOG_SIMPLE("Invalid tls cmd: %s\r\n", argv[1]);
        return -1;
    }

    LOG_SIMPLE("tls %s ret: %d\r\n", argv[1], ret);
    return ret;
}

debug_cmd_reg_t tls_test_cmd_table[] = {
    {"tls",    "test tls function.",      tls_test_cmd_deal},
};

static void tls_test_cmd_register(void)
{
    debug_cmdline_register(tls_test_cmd_table, sizeof(tls_test_cmd_table) / sizeof(tls_test_cmd_table[0]));
}

void tls_test_register(void)
{
    driver_cmd_register_callback("tls_test", tls_test_cmd_register);
}
