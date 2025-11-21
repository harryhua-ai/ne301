#ifndef __MS_MQTT_CLIENT_PRIV_H__
#define __MS_MQTT_CLIENT_PRIV_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "mqtt_outbox.h"
#include "ms_mqtt_client.h"

/// @brief MQTT client
struct ms_mqtt_client
{
    ms_mqtt_state_t     state;
    ms_mqtt_config_t    *config;
    ms_mqtt_event_data_t event;
    ms_mqtt_client_event_handler_t event_handler_list[MS_MQTT_CLIENT_MAX_EVENT_FUNC_SIZE];
    void                *event_user_data[MS_MQTT_CLIENT_MAX_EVENT_FUNC_SIZE];

    uint8_t run;
    uint8_t wait_for_ping_resp;
    uint16_t msg_id;
    uint32_t last_retransmit_tick;
    uint32_t last_ping_tick;
    uint32_t keepalive_tick;
    uint32_t reconnect_tick;

    outbox_handle_t     outbox;
    EventGroupHandle_t  status_bits;
    SemaphoreHandle_t   lock;
    TaskHandle_t        task_handle;
    void               *network_handle;
};

#ifdef __cplusplus
}
#endif

#endif /* __MS_MQTT_CLIENT_H__ */

