#include "Log/debug.h"
#include "mem.h"
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "ms_network.h"
#include "ms_network_test.h"

static ms_network_handle_t g_test_network = NULL;

static void ms_network_test_recv(void *args)
{
    ms_network_handle_t *network = (ms_network_handle_t *)args;
    uint8_t *recv_buf = NULL;
    int rlen = 0;

    LOG_SIMPLE("Network recv thread start.");
    do {
        if (network == NULL || *network == NULL) {
            LOG_SIMPLE("Invalid args!");
            return;
        }

        recv_buf = hal_mem_alloc(1024, MEM_LARGE);
        if (recv_buf == NULL) {
            LOG_SIMPLE("Memory alloc failed!");
            break;
        }
    
        while (1) {
            memset(recv_buf, 0, 1024);
            rlen = ms_network_recv(*network, recv_buf, 1023, 5000);
            if (rlen > 0) {
                LOG_SIMPLE("Network recv %d bytes:", rlen);
                printf("%s\r\n", recv_buf);
            } else if (rlen == NET_ERR_TIMEOUT) {
                // LOG_SIMPLE("Recv timeout.");
            } else {
                LOG_SIMPLE("Network recv failed(%d), exit.", rlen);
                break;
            }
        }
    } while (0);

    if (recv_buf) hal_mem_free(recv_buf);
    ms_network_deinit(*network);
    *network = NULL;
    LOG_SIMPLE("Network recv thread exit.");
}

static void ms_network_test_help(void)
{
    LOG_SIMPLE("Usage: tcp cnt [host] [port]");
    LOG_SIMPLE("       tcp send_str [data]");
    LOG_SIMPLE("       tcp send_buf [len] [char]");
    LOG_SIMPLE("       tcp close");
}

int ms_network_test_cmd_deal(int argc, char* argv[])
{
    int i = 0;
    
    if (argc < 2) {
        ms_network_test_help();
        return -1;
    }

    if (strcmp(argv[1], "cnt") == 0) {
        if (g_test_network != NULL) {
            LOG_SIMPLE("Please close first!");
            return -1;
        }
        const char *host = (argc > 2) ? argv[2] : "www.baidu.com";
        int port = (argc > 3) ? atoi(argv[3]) : 80;
        g_test_network = ms_network_init(NULL);
        if (g_test_network == NULL) {
            LOG_SIMPLE("Network init failed!");
            return -1;
        }
        LOG_SIMPLE("Network init success.");
        
        if (ms_network_connect(g_test_network, host, port, 5000) != 0) {
            LOG_SIMPLE("Network connect %s:%d failed!", host, port);
            ms_network_deinit(g_test_network);
            g_test_network = NULL;
            return -1;
        }
        LOG_SIMPLE("Network connect %s:%d success.", host, port);

        sys_thread_new("ms_net_test_recv", ms_network_test_recv, (void *)&g_test_network, DEFAULT_THREAD_STACKSIZE, 54);
        LOG_SIMPLE("Create network recv task success.");
    } else if (strcmp(argv[1], "send_str") == 0) {
        if (g_test_network == NULL) {
            LOG_SIMPLE("Please connect first!");
            return -1;
        }
        if (argc < 3) {
            LOG_SIMPLE("Please input data to send!");
            return -1;
        }
        int slen = ms_network_send(g_test_network, (uint8_t *)argv[2], strlen(argv[2]), 5000);
        if (slen < 0) {
            LOG_SIMPLE("Network send failed(%d)!", slen);
            return -1;
        }
        LOG_SIMPLE("Network send %d bytes success.", slen);
    } else if (strcmp(argv[1], "send_buf") == 0) {
        if (g_test_network == NULL) {
            LOG_SIMPLE("Please connect first!");
            return -1;
        }
        if (argc < 4) {
            LOG_SIMPLE("Please input len and char to send!");
            return -1;
        }
        int len = atoi(argv[2]);
        if (len <= 0 || len > 40960) {
            LOG_SIMPLE("Invalid len!");
            return -1;
        }
        char ch = argv[3][0];
        uint8_t *send_buf = hal_mem_alloc(len, MEM_LARGE);
        if (send_buf == NULL) {
            LOG_SIMPLE("Memory alloc failed!");
            return -1;
        }
        for (i = 0; i < len; i++) send_buf[i] = ch;
        send_buf[len] = 0;

        int slen = ms_network_send(g_test_network, send_buf, len, 50000);
        if (slen < 0) {
            LOG_SIMPLE("Network send failed(%d)!", slen);
            hal_mem_free(send_buf);
            return -1;
        }
        LOG_SIMPLE("Network send %d bytes success.", slen);
        hal_mem_free(send_buf);
    } else if (strcmp(argv[1], "close") == 0) {
        if (g_test_network == NULL) {
            LOG_SIMPLE("Please connect first!");
            return -1;
        }
        ms_network_close(g_test_network);
        LOG_SIMPLE("Network closed.");
    } else {
        ms_network_test_help();
        return -1;
    }

    return 0;
}

debug_cmd_reg_t ms_network_test_cmd_table[] = {
    {"tcp",    "tcp net test.",      ms_network_test_cmd_deal},
};

static void ms_network_test_cmd_register(void)
{
    debug_cmdline_register(ms_network_test_cmd_table, sizeof(ms_network_test_cmd_table) / sizeof(ms_network_test_cmd_table[0]));
}

void ms_network_test_register(void)
{
    driver_cmd_register_callback("ms_net_test", ms_network_test_cmd_register);
}
