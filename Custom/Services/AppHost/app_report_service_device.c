#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "app_report_service.h"
#include "app_report_service_device.h"

#include "app_installer.h"
#include "cmsis_os2.h"
#include "stm32n6xx_hal.h"
#include "factory_test.h"
#include "netif_manager.h"
#include "mqtt_service.h"
#include "webhook_service.h"
#include "debug.h"
#include "storage.h"

#ifndef APP_REPORT_DEV_PUMP_PERIOD_MS
#define APP_REPORT_DEV_PUMP_PERIOD_MS 500u
#endif
#define APP_REPORT_DEV_TASK_STACK 2048u
#define APP_REPORT_DEV_TASK_PRIORITY 24u
#ifndef APP_REPORT_DEV_ACK_WAIT_MS
#define APP_REPORT_DEV_ACK_WAIT_MS 2000u
#endif
#ifndef APP_REPORT_DEV_ACK_POLL_MS
#define APP_REPORT_DEV_ACK_POLL_MS 250u
#endif
#ifndef APP_REPORT_DEV_STOP_JOIN_MS
#define APP_REPORT_DEV_STOP_JOIN_MS 5000u
#endif
#ifndef APP_REPORT_DEV_STOP_POLL_MS
#define APP_REPORT_DEV_STOP_POLL_MS 10u
#endif
#ifndef APP_REPORT_DEV_TRANSPORT_MAX_MS
#define APP_REPORT_DEV_TRANSPORT_MAX_MS 15000u
#endif
#define APP_REPORT_DEV_JOIN_FLOOR_MS \
    (APP_REPORT_DEV_ACK_WAIT_MS + APP_REPORT_DEV_TRANSPORT_MAX_MS + \
     APP_REPORT_DEV_PUMP_PERIOD_MS + (2u * APP_REPORT_DEV_STOP_POLL_MS) + \
     200u)
#define APP_REPORT_DEV_MAC_STR_LEN 17u

#define DEV_LIFE_STOPPED 0u
#define DEV_LIFE_RUNNING 1u
#define DEV_LIFE_STOPPING 2u
#define DEV_LIFE_QUIESCE_PENDING 3u

static osMutexId_t g_report_lock;
static osMutexId_t g_life_lock;
static volatile uint32_t g_life_state = DEV_LIFE_STOPPED;
static osThreadId_t g_pump_thread;
static volatile aicam_bool_t g_running;
static volatile aicam_bool_t g_pump_exit_done;
static aicam_bool_t g_dev_initialized;
static aicam_bool_t g_dev_boot_valid;
static uint32_t g_dev_boot_id;
static const osMutexAttr_t g_report_lock_attr = {
    "app_report_lock", osMutexRecursive, NULL, 0u
};
static const osThreadAttr_t g_pump_thread_attr = {
    .name = "app_report_pump",
    .stack_size = APP_REPORT_DEV_TASK_STACK,
    .priority = APP_REPORT_DEV_TASK_PRIORITY,
};

static int32_t dev_policy_storage_read(void *user, void *out,
                                       uint32_t cap, uint32_t *out_len)
{
    int rc;

    (void)user;
    if (out == NULL || out_len == NULL) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    *out_len = 0u;
    rc = storage_nvs_read(NVS_USER, "app_report_policy", out, cap);
    if (rc == -ENOENT) {
        return APP_REPORT_POLICY_STORAGE_NOT_FOUND;
    }
    if (rc < 0 || (uint32_t)rc > cap) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    *out_len = (uint32_t)rc;
    return APP_REPORT_POLICY_STORAGE_OK;
}

static int32_t dev_policy_storage_write(void *user, const void *data,
                                        uint32_t len)
{
    int rc;

    (void)user;
    if (data == NULL || len == 0u) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    rc = storage_nvs_write(NVS_USER, "app_report_policy", data, len);
    return (rc >= 0 && (uint32_t)rc == len) ?
           APP_REPORT_POLICY_STORAGE_OK : APP_REPORT_POLICY_STORAGE_ERROR;
}

static const app_report_policy_storage_t g_dev_policy_storage = {
    NULL,
    dev_policy_storage_read,
    dev_policy_storage_write
};

static void report_lock_enter(void)
{
    if (g_report_lock != NULL) {
        (void)osMutexAcquire(g_report_lock, osWaitForever);
    }
}

static void report_lock_exit(void)
{
    if (g_report_lock != NULL) {
        (void)osMutexRelease(g_report_lock);
    }
}

static aicam_bool_t dev_locks_ensure(void)
{
    if (g_life_lock == NULL) {
        g_life_lock = osMutexNew(&g_report_lock_attr);
    }
    if (g_life_lock == NULL) {
        return AICAM_FALSE;
    }
    if (g_report_lock == NULL) {
        g_report_lock = osMutexNew(&g_report_lock_attr);
        if (g_report_lock == NULL) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t dev_locks_ready(void)
{
    return (g_life_lock != NULL && g_report_lock != NULL) ? AICAM_TRUE
                                                        : AICAM_FALSE;
}

static aicam_bool_t dev_identity_device_id(void *user, char *out, uint32_t cap)
{
    uint8_t mac[6];
    netif_info_t netif;

    (void)user;
    if (out == NULL || cap < (APP_REPORT_DEV_MAC_STR_LEN + 1u)) {
        return AICAM_FALSE;
    }
    if (factory_mac_get_burned(mac) == 0) {
        (void)snprintf(out, cap, NETIF_MAC_STR_FMT,
                       NETIF_MAC_PARAMETER(mac));
        return AICAM_TRUE;
    }
    if (nm_get_netif_info(NETIF_NAME_WIFI_AP, &netif) == AICAM_OK) {
        (void)snprintf(out, cap, NETIF_MAC_STR_FMT,
                       NETIF_MAC_PARAMETER(netif.if_mac));
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

static uint32_t dev_identity_boot_id(void *user)
{
    uint32_t v;

    (void)user;
    if (g_dev_boot_valid != AICAM_TRUE) {
        v = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
        v ^= (uint32_t)osKernelGetTickCount();
        if (v == 0u) {
            v = 1u;
        }
        g_dev_boot_id = v;
        g_dev_boot_valid = AICAM_TRUE;
    }
    return g_dev_boot_id;
}

static const app_report_identity_t g_dev_identity = {
    NULL,
    dev_identity_device_id,
    dev_identity_boot_id
};

static uint32_t dev_mqtt_ready(void *user)
{
    mqtt_service_topic_config_t tc;

    (void)user;
    if (mqtt_service_get_topic_config(&tc) != AICAM_OK) {
        return APP_REPORT_CHANNEL_WAITING;
    }
    if (tc.data_report_topic[0] == '\0') {
        return APP_REPORT_CHANNEL_UNCONFIGURED;
    }
    if (mqtt_service_is_running() != AICAM_TRUE) {
        return APP_REPORT_CHANNEL_WAITING;
    }
    return APP_REPORT_CHANNEL_READY;
}

static int dev_mqtt_publish(void *user, const uint8_t *json, uint32_t len)
{
    mqtt_service_topic_config_t tc;
    int msg_id;
    int qos;
    uint32_t start;
    aicam_bool_t acked = AICAM_FALSE;

    (void)user;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (mqtt_service_get_topic_config(&tc) != AICAM_OK ||
        tc.data_report_topic[0] == '\0') {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    qos = tc.data_report_qos;
    if (qos < 0) {
        qos = 0;
    }
    if (qos > 2) {
        qos = 2;
    }
    if (app_report_service_transport_start(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_begin(APP_REPORT_CHANNEL_MQTT) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    msg_id = mqtt_service_publish(tc.data_report_topic, json, (int)len, qos, 0);
    if (msg_id < 0) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (qos == 0) {
        return APP_REPORT_TRANSPORT_OK;
    }
    start = (uint32_t)osKernelGetTickCount();
    while (((uint32_t)osKernelGetTickCount() - start) <
           APP_REPORT_DEV_ACK_WAIT_MS) {
        int acked_id = 0;
        aicam_result_t rc =
            mqtt_service_get_acked_msg_id(&acked_id, APP_REPORT_DEV_ACK_POLL_MS);
        if (rc == AICAM_OK && acked_id == msg_id) {
            acked = AICAM_TRUE;
            break;
        }
    }
    return (acked == AICAM_TRUE) ? APP_REPORT_TRANSPORT_OK
                                 : APP_REPORT_TRANSPORT_UNKNOWN;
}

static uint32_t dev_webhook_ready(void *user)
{
    webhook_config_t cfg;

    (void)user;
    if (json_config_get_webhook_config(&cfg) != AICAM_OK) {
        return APP_REPORT_CHANNEL_WAITING;
    }
    if (cfg.enable != AICAM_TRUE || cfg.url[0] == '\0') {
        return APP_REPORT_CHANNEL_UNCONFIGURED;
    }
    return APP_REPORT_CHANNEL_READY;
}

static int dev_webhook_post(void *user, const uint8_t *json, uint32_t len)
{
    (void)user;
    if (app_report_service_transport_entered(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_start(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (app_report_service_transport_begin(APP_REPORT_CHANNEL_WEBHOOK) !=
        APP_REPORT_ABI_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    if (webhook_service_push_json(json, len) != AICAM_OK) {
        return APP_REPORT_TRANSPORT_FAIL;
    }
    return APP_REPORT_TRANSPORT_OK;
}

static const app_report_transport_t g_dev_transport = {
    NULL,
    dev_mqtt_ready,
    dev_mqtt_publish,
    dev_webhook_ready,
    dev_webhook_post
};

static void app_report_pump_task(void *arg)
{
    (void)arg;
    while (g_running == AICAM_TRUE) {
        app_report_service_pump();
        if (g_running != AICAM_TRUE) {
            break;
        }
        (void)osDelay(APP_REPORT_DEV_PUMP_PERIOD_MS);
    }
    g_pump_exit_done = AICAM_TRUE;
}

static uint32_t dev_join_window_ms(void)
{
    if (APP_REPORT_DEV_STOP_JOIN_MS < APP_REPORT_DEV_JOIN_FLOOR_MS) {
        return APP_REPORT_DEV_JOIN_FLOOR_MS;
    }
    return APP_REPORT_DEV_STOP_JOIN_MS;
}

static aicam_bool_t dev_quiesce_wait(void)
{
    uint32_t start = (uint32_t)osKernelGetTickCount();
    uint32_t window = dev_join_window_ms();

    while (((uint32_t)osKernelGetTickCount() - start) < window) {
        if (g_pump_exit_done == AICAM_TRUE) {
            return AICAM_TRUE;
        }
        (void)osDelay(APP_REPORT_DEV_STOP_POLL_MS);
    }
    return (g_pump_exit_done == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

static void dev_finish_stop(void)
{
    g_pump_thread = NULL;
    app_report_service_restart();
    g_life_state = DEV_LIFE_STOPPED;
}

static aicam_result_t dev_pump_launch(void)
{
    app_report_service_set_policy_storage(&g_dev_policy_storage);
    app_report_service_set_committed_io(app_installer_device_io());
    if (g_dev_initialized != AICAM_TRUE) {
        app_report_service_init(&g_dev_identity, &g_dev_transport);
        g_dev_initialized = AICAM_TRUE;
    }
    app_report_service_set_lock(report_lock_enter, report_lock_exit);
    g_pump_exit_done = AICAM_FALSE;
    g_running = AICAM_TRUE;
    g_pump_thread = osThreadNew(app_report_pump_task, NULL, &g_pump_thread_attr);
    if (g_pump_thread == NULL) {
        g_running = AICAM_FALSE;
        g_pump_exit_done = AICAM_TRUE;
        LOG_SVC_ERROR("appreport: pump task create failed");
        return AICAM_ERROR;
    }
    LOG_SVC_INFO("appreport: started (boot=%u)", (unsigned int)app_report_service_boot_id());
    return AICAM_OK;
}

aicam_result_t app_report_service_device_start(void)
{
    aicam_result_t rc;

    if (dev_locks_ready() != AICAM_TRUE) {
        LOG_SVC_ERROR("appreport: lock create failed");
        return AICAM_ERROR;
    }
    (void)osMutexAcquire(g_life_lock, osWaitForever);
    if (g_life_state == DEV_LIFE_RUNNING) {
        (void)osMutexRelease(g_life_lock);
        return AICAM_OK;
    }
    if (g_life_state == DEV_LIFE_STOPPING) {
        (void)osMutexRelease(g_life_lock);
        LOG_SVC_ERROR("appreport: start while stopping");
        return AICAM_ERROR_BUSY;
    }
    if (g_life_state == DEV_LIFE_QUIESCE_PENDING) {
        if (dev_quiesce_wait() != AICAM_TRUE) {
            (void)osMutexRelease(g_life_lock);
            LOG_SVC_ERROR("appreport: start blocked, worker not quiesced");
            return AICAM_ERROR_BUSY;
        }
        dev_finish_stop();
    }
    rc = dev_pump_launch();
    if (rc == AICAM_OK) {
        g_life_state = DEV_LIFE_RUNNING;
    }
    (void)osMutexRelease(g_life_lock);
    return rc;
}

aicam_result_t app_report_service_device_stop(void)
{
    if (dev_locks_ready() != AICAM_TRUE) {
        LOG_SVC_ERROR("appreport: lock create failed");
        return AICAM_ERROR;
    }
    (void)osMutexAcquire(g_life_lock, osWaitForever);
    if (g_life_state == DEV_LIFE_STOPPED) {
        (void)osMutexRelease(g_life_lock);
        return AICAM_OK;
    }
    if (g_life_state == DEV_LIFE_STOPPING) {
        (void)osMutexRelease(g_life_lock);
        LOG_SVC_ERROR("appreport: stop already in progress");
        return AICAM_ERROR_BUSY;
    }
    g_life_state = DEV_LIFE_STOPPING;
    g_running = AICAM_FALSE;
    if (dev_quiesce_wait() != AICAM_TRUE) {
        g_life_state = DEV_LIFE_QUIESCE_PENDING;
        LOG_SVC_ERROR("appreport: stop failed, worker not quiesced, "
                      "state kept");
        (void)osMutexRelease(g_life_lock);
        return AICAM_ERROR;
    }
    dev_finish_stop();
    (void)osMutexRelease(g_life_lock);
    LOG_SVC_INFO("appreport: stopped");
    return AICAM_OK;
}

aicam_bool_t app_report_service_device_running(void)
{
    return (g_life_state == DEV_LIFE_STOPPED) ? AICAM_FALSE : AICAM_TRUE;
}

static const char *report_state_str(uint32_t state)
{
    switch (state) {
    case APP_REPORT_MQTT_NOT_CONFIGURED:
        return "NOT_CONFIGURED";
    case APP_REPORT_MQTT_PENDING:
        return "PENDING";
    case APP_REPORT_MQTT_TRANSPORT_SUCCEEDED:
        return "TRANSPORT_SUCCEEDED";
    case APP_REPORT_MQTT_TRANSPORT_FAILED:
        return "TRANSPORT_FAILED";
    case APP_REPORT_MQTT_UNKNOWN:
        return "UNKNOWN";
    case APP_REPORT_MQTT_CANCELLED:
        return "CANCELLED";
    case APP_REPORT_MQTT_RETRY_SUPPRESSED:
        return "RETRY_SUPPRESSED";
    case APP_REPORT_MQTT_DISABLED:
        return "DISABLED";
    case APP_REPORT_MQTT_POLICY_UNKNOWN:
        return "POLICY_UNKNOWN";
    default:
        return "?";
    }
}

static int appreport_cmd(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
        return (app_report_service_device_start() == AICAM_OK) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        return (app_report_service_device_stop() == AICAM_OK) ? 0 : 1;
    }
    if (argc < 2 || strcmp(argv[1], "info") == 0) {
        app_report_stats_t st;
        uint32_t i;

        LOG_SIMPLE("appreport: running=%d boot=%u queue=%u\r\n",
                   (int)app_report_service_device_running(),
                   (unsigned int)app_report_service_boot_id(),
                   (unsigned int)app_report_service_queue_count());
        app_report_service_get_stats(&st);
        LOG_SIMPLE("appreport: accepted=%u dedup=%u rej(param=%u schema=%u "
                   "quota=%u busy=%u unauth=%u ident=%u)\r\n",
                   (unsigned int)st.accepted, (unsigned int)st.dedup_hits,
                   (unsigned int)st.rejected_param,
                   (unsigned int)st.rejected_schema,
                   (unsigned int)st.rejected_quota,
                   (unsigned int)st.rejected_busy,
                   (unsigned int)st.rejected_unauthorized,
                   (unsigned int)st.rejected_identity);
        for (i = 0u; i < APP_REPORT_QUEUE_SLOTS; i++) {
            app_report_entry_info_t info;
            if (app_report_service_entry(i, &info) != AICAM_TRUE) {
                continue;
            }
            LOG_SIMPLE("appreport: slot=%u seq=%u len=%u mqtt=%s(%u) "
                       "webhook=%s(%u)\r\n",
                       (unsigned int)i, (unsigned int)info.report_seq,
                       (unsigned int)info.payload_len,
                       report_state_str(info.mqtt_state),
                       (unsigned int)info.mqtt_attempts,
                       report_state_str(info.webhook_state),
                       (unsigned int)info.webhook_attempts);
        }
        return 0;
    }
    LOG_SIMPLE("usage: appreport info | appreport start | appreport stop\r\n");
    return 1;
}

static const debug_cmd_reg_t g_appreport_cmd_table[] = {
    { "appreport", "appreport info | appreport start | appreport stop",
      appreport_cmd }
};

void app_report_service_device_cmd_register(void)
{
    if (dev_locks_ensure() != AICAM_TRUE) {
        LOG_SVC_ERROR("appreport: lock create failed");
        return;
    }
    debug_register_commands(g_appreport_cmd_table, 1u);
}
