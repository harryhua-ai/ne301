#ifndef __SI91X_MQTT_CLIENT_H__
#define __SI91X_MQTT_CLIENT_H__

#ifdef __cplusplus
    extern "C" {
#endif

#include "ms_mqtt_client.h"

int si91x_mqtt_client_init(const ms_mqtt_config_t *config);
int si91x_mqtt_client_connnect(void);
int si91x_mqtt_client_publish(const char *topic, const char *data, int data_len, int qos, int retain);
int si91x_mqtt_client_subscribe(const char *topic, int qos);
int si91x_mqtt_client_unsubscribe(const char *topic);
int si91x_mqtt_client_disconnect(void);
int si91x_mqtt_client_deinit(void);

int si91x_mqtt_client_connnect_sync(uint32_t timeout_ms);
int si91x_mqtt_client_publish_sync(const char *topic, const char *data, int data_len, int qos, int retain, uint32_t timeout_ms);
int si91x_mqtt_client_subscribe_sync(const char *topic, int qos, uint32_t timeout_ms);
int si91x_mqtt_client_unsubscribe_sync(const char *topic, uint32_t timeout_ms);
int si91x_mqtt_client_disconnect_sync(uint32_t timeout_ms);

int si91x_mqtt_client_register_event(ms_mqtt_client_event_handler_t event_handler, void *user_arg);
int si91x_mqtt_client_unregister_event(ms_mqtt_client_event_handler_t event_handler);
ms_mqtt_state_t si91x_mqtt_client_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __SI91X_MQTT_CLIENT_H__ */
