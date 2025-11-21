#include <stdio.h>
#include <string.h>
#include "common_utils.h"
#include "lwip/etharp.h"
#include "netif/ppp/pppos.h"
#include "netif/ppp/pppapi.h"
#include "Log/debug.h"
#include "mem.h"
#include "cat1.h"
#include "netif_manager.h"
#include "eg912u_gl_netif.h"

#define EG912U_EVENT_RECV_COMPLETE              (1 << 0)
#define EG912U_EVENT_READ_TASK_EXIT_REQ         (1 << 1)
#define EG912U_EVENT_READ_TASK_EXIT_ACK         (1 << 2)
#define EG912U_EVENT_PPP_CNT                    (1 << 3)
#define EG912U_EVENT_PPP_EXIT                   (1 << 4)
#define EG912U_EVENT_ENABLE_RECV_ERROR          (1 << 5)

/// @brief 4G network interface dial control block
static ppp_pcb *eg912u_ppp_pcb = NULL;
/// @brief 4G network interface
static struct netif eg912u_netif = {
    .name = {NETIF_NAME_4G_CAT1[0], NETIF_NAME_4G_CAT1[1]},
};
/// @brief 4G network interface status information
static cellularStatusAttr_t eg912u_cellular_status = {0};
/// @brief 4G network interface signal quality information
static cellularSignalQuality_t eg912u_signal_quality = {0};
/// @brief 4G network interface parameter information
static cellularParamAttr_t eg912u_param_attr = {0};
/// @brief 4G network interface event
static osEventFlagsId_t eg912u_events = NULL;
/// @brief 4G network interface lock
static osMutexId_t eg912u_mutex = NULL;
/// @brief 4G network interface dial control block lock
static osMutexId_t eg912u_ppp_mutex = NULL;
/// @brief 4G data processing thread ID
static osThreadId_t eg912u_read_thread_ID = NULL;
/// @brief 4G data processing thread stack
static uint8_t g912u_read_stack[4096] ALIGN_32 IN_PSRAM = {0};
/// @brief 4G data processing thread attributes
static const osThreadAttr_t g912u_read_attr = {
    .name       = "g912u_read",
    .priority   = osPriorityRealtime4,
    .stack_mem  = g912u_read_stack,
    .stack_size = sizeof(g912u_read_stack),
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
};

#define EG912U_BUF_SIZE     (2 * 1024)
static uint8_t eg912u_wbuf[EG912U_BUF_SIZE] ALIGN_32 UNCACHED = {0};
static uint8_t eg912u_rbuf[EG912U_BUF_SIZE] ALIGN_32 UNCACHED = {0};
static uint8_t eg912u_rbuf2[EG912U_BUF_SIZE] ALIGN_32 = {0};
static uint16_t eg912u_rbuf_len = 0;

static int eg912u_update_info(void)
{
    int ret = 0;
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL) return -1;

    ret = device_ioctl(cat1_dev, CAT1_CMD_GET_CSQ, (unsigned char*)&eg912u_signal_quality, sizeof(cellularSignalQuality_t));
    if (ret != 0) return ret;

    ret = device_ioctl(cat1_dev, CAT1_CMD_GET_STATUS, (unsigned char*)&eg912u_cellular_status, sizeof(cellularStatusAttr_t));
    if (ret != 0) return ret;

    ret = device_ioctl(cat1_dev, CAT1_CMD_GET_PARAM, (unsigned char*)&eg912u_param_attr, sizeof(cellularParamAttr_t));
    if (ret != 0) return ret;

    return ret;
}

uint32_t eg912u_ppp_output_cb(ppp_pcb *pcb, const void *data, u32_t len, void *ctx)
{
    int ret = 0;
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL || eg912u_ppp_pcb == NULL) return -1;

    if (len > EG912U_BUF_SIZE) len = EG912U_BUF_SIZE;
    memcpy(eg912u_wbuf, data, len);
    ret = device_ioctl(cat1_dev, CAT1_CMD_PPP_SEND, (unsigned char *)eg912u_wbuf, len);
    if (ret != 0) {
        LOG_DRV_ERROR("cat1 send data failed(ret = %d)!", ret);
        return ret;
    }
    
    return len;
}

void eg912u_ppp_status_cb(ppp_pcb* pcb, int err_code, void *ctx)
{
    LOG_DRV_INFO("ppp status: %d", err_code);

    if (err_code == PPPERR_NONE) {
#if PPP_IPV4_SUPPORT
        printf("ppp ip_addr = %s\r\n", ipaddr_ntoa(&eg912u_netif.ip_addr));
        printf("ppp gw      = %s\r\n", ipaddr_ntoa(&eg912u_netif.gw));
        printf("ppp netmask = %s\r\n", ipaddr_ntoa(&eg912u_netif.netmask));
#endif /* PPP_IPV4_SUPPORT */
        osEventFlagsSet(eg912u_events, EG912U_EVENT_PPP_CNT);
    } else if (err_code == PPPERR_USER) {
        osEventFlagsSet(eg912u_events, EG912U_EVENT_PPP_EXIT);
    }
}

void eg912u_uart_recv_callback(void *handle, uint16_t len)
{
    if (len > (EG912U_BUF_SIZE - eg912u_rbuf_len)) len = (EG912U_BUF_SIZE - eg912u_rbuf_len);
    memcpy(&eg912u_rbuf2[eg912u_rbuf_len], eg912u_rbuf, len);
    eg912u_rbuf_len += len;
    if (cat1_ppp_enable_recv_isr(eg912u_rbuf, EG912U_BUF_SIZE) != 0) {
        osEventFlagsSet(eg912u_events, EG912U_EVENT_ENABLE_RECV_ERROR);
    }
    osEventFlagsSet(eg912u_events, EG912U_EVENT_RECV_COMPLETE);
}

void eg912u_read_thread(void *arg)
{
    int ret = 0;
    uint32_t event = 0;
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL) {
        osEventFlagsSet(eg912u_events, EG912U_EVENT_READ_TASK_EXIT_ACK);
        osThreadExit();
    }
    while (1) {
        event = osEventFlagsWait(eg912u_events, EG912U_EVENT_RECV_COMPLETE | EG912U_EVENT_READ_TASK_EXIT_REQ | EG912U_EVENT_ENABLE_RECV_ERROR, osFlagsWaitAny, osWaitForever);
        if (event & osFlagsError) continue;

        if (event & EG912U_EVENT_ENABLE_RECV_ERROR) {
            LOG_DRV_ERROR("cat1 enable recv isr error!");
            ret = device_ioctl(cat1_dev, CAT1_CMD_PPP_RECV, eg912u_rbuf, EG912U_BUF_SIZE);
            if (ret != 0) osEventFlagsSet(eg912u_events, EG912U_EVENT_ENABLE_RECV_ERROR);
        }
        if (event & EG912U_EVENT_RECV_COMPLETE) {
            osMutexAcquire(eg912u_ppp_mutex, osWaitForever);
            if (eg912u_ppp_pcb) pppos_input(eg912u_ppp_pcb, eg912u_rbuf2, eg912u_rbuf_len);
            eg912u_rbuf_len = 0;
            osMutexRelease(eg912u_ppp_mutex);
        }
        if (event & EG912U_EVENT_READ_TASK_EXIT_REQ) {
            osEventFlagsSet(eg912u_events, EG912U_EVENT_READ_TASK_EXIT_ACK);
            osThreadExit();
        }
    }
}

int eg912u_netif_init(void)
{
    int ret = 0;
    uint32_t start_tick = 0, end_tick, diff_tick = 0;
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev != NULL) return -1;

    cat1_register();

    eg912u_events = osEventFlagsNew(NULL);
    if (eg912u_events == NULL) {
        ret = AICAM_ERROR_NO_MEMORY;
        goto eg912u_netif_init_exit;
    }
    eg912u_ppp_mutex = osMutexNew(NULL);
    if (eg912u_ppp_mutex == NULL) {
        ret = AICAM_ERROR_NO_MEMORY;
        goto eg912u_netif_init_exit;
    }
    eg912u_read_thread_ID = osThreadNew(eg912u_read_thread, NULL, &g912u_read_attr);
    if (eg912u_read_thread_ID == NULL) {
        ret = AICAM_ERROR_NO_MEMORY;
        goto eg912u_netif_init_exit;
    }
    
    start_tick = HAL_GetTick();
    do {
        ret = eg912u_update_info();
        if (ret != 0) {
            end_tick = HAL_GetTick();
            diff_tick = (end_tick >= start_tick) ? (end_tick - start_tick) : (0xFFFFFFFFU - start_tick + end_tick);
            if (diff_tick >= NETIF_4G_CAT1_INIT_TIMEOUT_MS) {
                ret = AICAM_ERROR_TIMEOUT;
                LOG_DRV_ERROR("eg912u update info timeout!");
                break;
            }
            osDelay(100);
        }
    } while (ret != 0);

eg912u_netif_init_exit:
    if (ret != 0) {
        if (eg912u_read_thread_ID != NULL) {
            osEventFlagsSet(eg912u_events, EG912U_EVENT_READ_TASK_EXIT_REQ);
            osEventFlagsWait(eg912u_events, EG912U_EVENT_READ_TASK_EXIT_ACK, osFlagsWaitAny, osWaitForever);
            osThreadTerminate(eg912u_read_thread_ID);
            eg912u_read_thread_ID = NULL;
        }
        if (eg912u_events != NULL) {
            osEventFlagsDelete(eg912u_events);
            eg912u_events = NULL;
        }
        if (eg912u_ppp_mutex != NULL) {
            osMutexDelete(eg912u_ppp_mutex);
            eg912u_ppp_mutex = NULL;
        }
        cat1_unregister();
    }
    return ret;
}

int eg912u_netif_up(void)
{
    int ret = 0;
    uint32_t event = 0;
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL || eg912u_ppp_pcb) return AICAM_ERROR_INVALID_PARAM;

    // Set APN
    ret = device_ioctl(cat1_dev, CAT1_CMD_SET_PARAM, (unsigned char *)&eg912u_param_attr, sizeof(cellularParamAttr_t));
    if (ret != 0) {
        LOG_DRV_ERROR("cat1 set param failed(ret = %d)!", ret);
        return ret;
    }

    ret = device_ioctl(cat1_dev, CAT1_CMD_INTO_PPP, (unsigned char *)&eg912u_uart_recv_callback, sizeof(cat1_recv_callback_t));
    if (ret != 0) {
        LOG_DRV_ERROR("cat1 into ppp failed(ret = %d)!", ret);
        return ret;
    }

    ret = device_ioctl(cat1_dev, CAT1_CMD_PPP_RECV, eg912u_rbuf, EG912U_BUF_SIZE);
    if (ret != 0) {
        LOG_DRV_ERROR("cat1 ppp recv failed(ret = %d)!", ret);
        device_ioctl(cat1_dev, CAT1_CMD_EXIT_PPP, NULL, 0);
        return ret;
    }

    osMutexAcquire(eg912u_ppp_mutex, osWaitForever);
    eg912u_ppp_pcb = pppos_create(&eg912u_netif, eg912u_ppp_output_cb, eg912u_ppp_status_cb, NULL);
    if (eg912u_ppp_pcb == NULL) {
        LOG_DRV_ERROR("create pppos failed!");
        device_ioctl(cat1_dev, CAT1_CMD_EXIT_PPP, NULL, 0);
        ret = AICAM_ERROR_NO_MEMORY;
    } else {
        eg912u_netif.name[0] = NETIF_NAME_4G_CAT1[0];
        eg912u_netif.name[1] = NETIF_NAME_4G_CAT1[1];
        osEventFlagsClear(eg912u_events, EG912U_EVENT_PPP_CNT | EG912U_EVENT_PPP_EXIT);
        ret = ppp_connect(eg912u_ppp_pcb, 0);
        if (ret != ERR_OK) {
            LOG_DRV_ERROR("ppp_connect failed(ret = %d)!", ret);
            eg912u_netif_down();
        }
    }
    osMutexRelease(eg912u_ppp_mutex);

    if (ret == 0) {
        event = osEventFlagsWait(eg912u_events, EG912U_EVENT_PPP_CNT, osFlagsWaitAny, NETIF_4G_CAT1_CNT_TIMEOUT_MS);
        if (event & osFlagsError) {
            ret = AICAM_ERROR_TIMEOUT;
            LOG_DRV_ERROR("ppp_connect timeout!");
            eg912u_netif_down();
        }
    }
    
    return ret;
}

int eg912u_netif_down(void)
{
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL) return AICAM_ERROR_INVALID_PARAM;

    osMutexAcquire(eg912u_ppp_mutex, osWaitForever);
    if (eg912u_ppp_pcb) {
        pppapi_close(eg912u_ppp_pcb, 0);
        osMutexRelease(eg912u_ppp_mutex);
        osEventFlagsWait(eg912u_events, EG912U_EVENT_PPP_EXIT, osFlagsWaitAny, NETIF_4G_CAT1_EXIT_TIMEOUT_MS);
        device_ioctl(cat1_dev, CAT1_CMD_EXIT_PPP, NULL, 0);
        osMutexAcquire(eg912u_ppp_mutex, osWaitForever);
        pppapi_free(eg912u_ppp_pcb);
        eg912u_ppp_pcb = NULL;
    }
    osMutexRelease(eg912u_ppp_mutex);

    return 0;
}

void eg912u_netif_deinit(void)
{
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL) return;

    eg912u_netif_down();
    if (eg912u_read_thread_ID != NULL) {
        osEventFlagsSet(eg912u_events, EG912U_EVENT_READ_TASK_EXIT_REQ);
        osEventFlagsWait(eg912u_events, EG912U_EVENT_READ_TASK_EXIT_ACK, osFlagsWaitAny, osWaitForever);
        osThreadTerminate(eg912u_read_thread_ID);
        eg912u_read_thread_ID = NULL;
    }
    if (eg912u_events != NULL) {
        osEventFlagsDelete(eg912u_events);
        eg912u_events = NULL;
    }
    if (eg912u_ppp_mutex != NULL) {
        osMutexDelete(eg912u_ppp_mutex);
        eg912u_ppp_mutex = NULL;
    }
    cat1_unregister();
}

int eg912u_netif_config(netif_config_t *netif_cfg)
{
    if (netif_cfg == NULL) return AICAM_ERROR_INVALID_PARAM;

    strcpy(eg912u_param_attr.apn, netif_cfg->cellular_cfg.apn);
    strcpy(eg912u_param_attr.pin, netif_cfg->cellular_cfg.pin);
#if LWIP_NETIF_HOSTNAME
    if (netif_cfg->host_name != NULL) {
        eg912u_netif.hostname = netif_cfg->host_name;
    }
#endif

    return AICAM_OK;
}

int eg912u_netif_info(netif_info_t *netif_info)
{
    device_t *cat1_dev = NULL;
    if (netif_info == NULL) return AICAM_ERROR_INVALID_PARAM;

#if LWIP_NETIF_HOSTNAME
    netif_info->host_name = eg912u_netif.hostname;
#else
    netif_info->host_name = NULL;
#endif
    netif_info->if_name = NETIF_NAME_4G_CAT1;
    netif_info->type = NETIF_TYPE_4G;
    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL) netif_info->state = NETIF_STATE_DEINIT;
    else if (eg912u_ppp_pcb == NULL) netif_info->state = NETIF_STATE_DOWN;
    else netif_info->state = NETIF_STATE_UP;
    netif_info->rssi = eg912u_signal_quality.dbm;
    memcpy(netif_info->ip_addr, &eg912u_netif.ip_addr, sizeof(netif_info->ip_addr));
    memcpy(netif_info->gw, &eg912u_netif.gw, sizeof(netif_info->gw));
    memcpy(netif_info->netmask, &eg912u_netif.netmask, sizeof(netif_info->netmask));
    memset(netif_info->fw_version, 0, sizeof(netif_info->fw_version));
    strcpy(netif_info->fw_version, eg912u_cellular_status.version);

    strcpy(netif_info->cellular_cfg.apn, eg912u_param_attr.apn);
    strcpy(netif_info->cellular_cfg.pin, eg912u_param_attr.pin);

    strcpy(netif_info->cellular_info.imei, eg912u_cellular_status.imei);
    strcpy(netif_info->cellular_info.imsi, eg912u_cellular_status.imsi);
    strcpy(netif_info->cellular_info.iccid, eg912u_cellular_status.iccid);
    strcpy(netif_info->cellular_info.model_name, eg912u_cellular_status.model);
    strcpy(netif_info->cellular_info.sim_status, eg912u_cellular_status.modemStatus);
    strcpy(netif_info->cellular_info.operator, eg912u_cellular_status.isp);
    netif_info->cellular_info.csq_value = eg912u_signal_quality.rssi;
    netif_info->cellular_info.ber_value = eg912u_signal_quality.ber;
    netif_info->cellular_info.csq_level = eg912u_signal_quality.level;

    return AICAM_OK;
}

netif_state_t eg912u_netif_state(void)
{
    device_t *cat1_dev = NULL;

    cat1_dev = device_find_pattern(CAT1_DEVICE_NAME, DEV_TYPE_NET);
    if (cat1_dev == NULL) return NETIF_STATE_DEINIT;
    else if (eg912u_ppp_pcb == NULL) return NETIF_STATE_DOWN;
    else return NETIF_STATE_UP;
}

struct netif *eg912u_netif_ptr(void)
{
    return &eg912u_netif;
}

int eg912u_netif_ctrl(const char *if_name, netif_cmd_t cmd, void *param)
{
    int ret = AICAM_ERROR;
    netif_state_t if_state = NETIF_STATE_DEINIT;
    if (eg912u_mutex == NULL) eg912u_mutex = osMutexNew(NULL);
    if (eg912u_mutex == NULL) return AICAM_ERROR_NO_MEMORY;

    osMutexAcquire(eg912u_mutex, osWaitForever);
    switch (cmd) {
        case NETIF_CMD_CFG:
            ret = eg912u_netif_config((netif_config_t *)param);
            break;
        case NETIF_CMD_INIT:
            ret = eg912u_netif_init();
            break;
        case NETIF_CMD_UP:
            ret = eg912u_netif_up();
            break;
        case NETIF_CMD_INFO:
            ret = eg912u_netif_info((netif_info_t *)param);
            break;
        case NETIF_CMD_STATE:
            if (param == NULL) ret = AICAM_ERROR_INVALID_PARAM;
            else {
                *(netif_state_t *)param = eg912u_netif_state();
                ret = AICAM_OK;
            }
            break;
        case NETIF_CMD_DOWN:
            ret = eg912u_netif_down();
            break;
        case NETIF_CMD_UNINIT:
            eg912u_netif_deinit();
            ret = AICAM_OK;
            break;
        case NETIF_CMD_CFG_EX:
            if_state = eg912u_netif_state();
            if (if_state == NETIF_STATE_UP) {
                ret = eg912u_netif_down();
                if (ret) break;
            }
            ret = eg912u_netif_config((netif_config_t *)param);
            if (ret) break;
            if (if_state == NETIF_STATE_UP) ret = eg912u_netif_up();
            break;
        default:
            ret = AICAM_ERROR_INVALID_PARAM;
            break;
    }
    osMutexRelease(eg912u_mutex);
    return ret;
}
