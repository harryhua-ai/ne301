
---

# MQTT Client Module Interface Documentation

## 1. Overview

`ms_mqtt_client.h` provides a FreeRTOS-based MQTT client interface, supporting MQTT 3.1/3.1.1 protocol, with connection management, topic subscription, message publishing, event callbacks, TLS authentication and other functions, suitable for efficient message interaction between IoT devices and the cloud.

---

## 2. Type Definitions

### 2.1 Client Handle

```c
typedef struct ms_mqtt_client *ms_mqtt_client_handle_t;
```
- Used to identify and operate an MQTT client instance.

---

### 2.2 Error Codes

```c
typedef enum {
    MQTT_ERR_OK = 0,
    MQTT_ERR_FAILED = -1,
    MQTT_ERR_INVALID_ARG = -2,
    ...
    MQTT_ERR_UNKNOWN = -0xff,
} mqtt_error_t;
```
- When the return value of all interfaces is negative, it indicates an exception. See the enumeration definition for specific meanings.

---

### 2.3 State Codes

```c
typedef enum {
    MQTT_STATE_STOPPED = 0,
    MQTT_STATE_STARTING,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAIT_RECONNECT,
    MQTT_STATE_MAX,
} ms_mqtt_state_t;
```
- Indicates the current running state of the client.

---

### 2.4 Event ID

```c
typedef enum {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_STARTED,
    ...
    MQTT_EVENT_USER,            
} ms_mqtt_event_id_t;
```
- Identifies MQTT lifecycle and message-related events.

---

### 2.5 Event Data Structure

```c
typedef struct ms_mqtt_event_data_t {
    ms_mqtt_event_id_t event_id;    
    ms_mqtt_client_handle_t client;
    int error_code;
    uint8_t *data;
    int data_len;
    uint8_t *topic;
    int topic_len;
    uint16_t msg_id;
    uint8_t session_present;
    uint8_t connect_rsp_code;
    uint8_t retain;
    int qos;
    uint8_t dup;
} ms_mqtt_event_data_t;
```
- Detailed data passed during event callback.

---

### 2.6 Topic Structure

```c
typedef struct topic_t {
    char *filter;
    int qos;
} ms_mqtt_topic_t;
```
- Used for multiple topic subscriptions.

---

### 2.7 Event Callback Prototype

```c
typedef void (*ms_mqtt_client_event_handler_t)(ms_mqtt_event_data_t *event_data, void *user_args);
```
- User implements callback function to handle events.

---

## 3. Configuration Structure Descriptions

### 3.1 MQTT Configuration Structure

```c
typedef struct {
    struct base_t { ... } base;
    struct authentication_t { ... } authentication;
    struct last_will_t { ... } last_will;
    struct task_t { ... } task;
    struct network_t { ... } network;
} ms_mqtt_config_t;
```
- **base**: Basic parameters (protocol version, server address, port, client ID, keep-alive, etc.)
- **authentication**: Authentication parameters (username, password, CA certificate, client certificate/key, hostname verification, etc.)
- **last_will**: Last will message (topic, content, QOS, retain flag, etc.)
- **task**: Task parameters (priority, stack size)
- **network**: Network parameters (reconnection, retransmission, timeout, buffer, etc.)

---

## 4. Macro Definition Descriptions

- `MS_MQTT_CLIENT_PING_TRY_COUNT`: PING failure retry count
- `MS_MQTT_CLIENT_MAX_EVENT_FUNC_SIZE`: Maximum number of event callback functions supported
- `MS_MQTT_CLIENT_TASK_BLOCK_TICK`: Task blocking time (Tick)

---

## 5. Interface Function Descriptions

### 5.1 Initialization and Destruction

#### `ms_mqtt_client_handle_t ms_mqtt_client_init(const ms_mqtt_config_t *config);`
- **Function**: Initialize MQTT client.
- **Parameters**: `config`: MQTT configuration structure pointer.
- **Return Value**: Returns client handle on success, NULL on failure.

#### `int ms_mqtt_client_destroy(ms_mqtt_client_handle_t client);`
- **Function**: Destroy client and release resources.
- **Parameters**: `client`: Client handle.
- **Return Value**: 0 on success, negative number on exception.

---

### 5.2 Start and Stop

#### `int ms_mqtt_client_start(ms_mqtt_client_handle_t client);`
- **Function**: Start client and establish connection.
- **Parameters**: `client`: Client handle.
- **Return Value**: 0 on success, negative number on exception.

#### `int ms_mqtt_client_stop(ms_mqtt_client_handle_t client);`
- **Function**: Stop client and disconnect.
- **Parameters**: `client`: Client handle.
- **Return Value**: 0 on success, negative number on exception.

#### `int ms_mqtt_client_disconnect(ms_mqtt_client_handle_t client);`
- **Function**: Disconnect and stop auto-reconnection.
- **Parameters**: `client`: Client handle.
- **Return Value**: 0 on success, negative number on exception.

#### `int ms_mqtt_client_reconnect(ms_mqtt_client_handle_t client);`
- **Function**: Notify client to reconnect.
- **Parameters**: `client`: Client handle.
- **Return Value**: 0 on success, negative number on exception.

---

### 5.3 Topic Subscription and Unsubscription

#### `int ms_mqtt_client_subscribe_single(ms_mqtt_client_handle_t client, char *topic, int qos);`
- **Function**: Subscribe to a single topic.
- **Parameters**: `client`: Client handle; `topic`: Topic string; `qos`: Maximum QOS.
- **Return Value**: >=0 message ID, <0 exception code.

#### `int ms_mqtt_client_subscribe_multiple(ms_mqtt_client_handle_t client, const ms_mqtt_topic_t *topic_list, int size);`
- **Function**: Batch subscribe to multiple topics.
- **Parameters**: `client`: Client handle; `topic_list`: Topic array; `size`: Count.
- **Return Value**: >=0 message ID, <0 exception code.

#### `int ms_mqtt_client_unsubscribe(ms_mqtt_client_handle_t client, char *topic);`
- **Function**: Unsubscribe from topic.
- **Parameters**: `client`: Client handle; `topic`: Topic string.
- **Return Value**: >=0 message ID, <0 exception code.

---

### 5.4 Message Publishing

#### `int ms_mqtt_client_publish(ms_mqtt_client_handle_t client, char *topic, uint8_t *data, int len, int qos, int retain);`
- **Function**: Synchronously publish message.
- **Parameters**:
  - `client`: Client handle
  - `topic`: Topic string
  - `data`: Message data
  - `len`: Message length
  - `qos`: Publish QOS
  - `retain`: Whether to retain
- **Return Value**: >=0 message ID, <0 exception code.

#### `int ms_mqtt_client_enqueue(ms_mqtt_client_handle_t client, char *topic, uint8_t *data, int len, int qos, int retain);`
- **Function**: Asynchronously publish message (add to send queue).
- **Parameters**: Same as above.
- **Return Value**: >=0 message ID, <0 exception code.

---

### 5.5 Event Callback Management

#### `int ms_mqtt_client_register_event(ms_mqtt_client_handle_t client, ms_mqtt_client_event_handler_t event_handler, void *user_arg);`
- **Function**: Register event callback function.
- **Parameters**: `client`: Client handle; `event_handler`: Callback function; `user_arg`: User data.
- **Return Value**: 0 on success, negative number on exception.

#### `int ms_mqtt_client_unregister_event(ms_mqtt_client_handle_t client, ms_mqtt_client_event_handler_t event_handler);`
- **Function**: Unregister event callback function.
- **Parameters**: `client`: Client handle; `event_handler`: Callback function.
- **Return Value**: 0 on success, negative number on exception.

---

### 5.6 Other Auxiliary Interfaces

#### `int ms_mqtt_client_get_outbox_size(ms_mqtt_client_handle_t client);`
- **Function**: Get number of unsent messages.
- **Parameters**: `client`: Client handle.
- **Return Value**: Message count.

#### `ms_mqtt_state_t ms_mqtt_client_get_state(ms_mqtt_client_handle_t client);`
- **Function**: Get current state of client.
- **Parameters**: `client`: Client handle.
- **Return Value**: State code.

---

## 6. Usage Recommendations

- **Thread Safety**: Interface implementation should ensure multi-thread safety, avoid time-consuming operations in user callbacks.
- **Resource Management**: Ensure client is stopped before destruction to avoid resource leaks.
- **Event Handling**: It is recommended to register event callbacks before starting the client to facilitate timely response to connection, message and other events.
- **Certificates and Authentication**: Prefer using paths, automatically use string length when length is 0.
- **Exception Handling**: Interface return values need to be checked, refer to `mqtt_error_t` when negative.

---

## 7. Example Code

```c
ms_mqtt_config_t config = { ... };
ms_mqtt_client_handle_t client = ms_mqtt_client_init(&config);

ms_mqtt_client_register_event(client, my_event_handler, NULL);
ms_mqtt_client_start(client);

ms_mqtt_client_subscribe_single(client, "test/topic", 1);
ms_mqtt_client_publish(client, "test/topic", (uint8_t *)"hello", 5, 1, 0);

ms_mqtt_client_stop(client);
ms_mqtt_client_destroy(client);
```

---

## 8. Version and Maintenance

- Document Version: V1.0
- Header File Version: 2025-09-10
- Maintainer: wicevi
- For detailed configuration parameters, event data descriptions, extension solutions and other materials, please contact the maintainer or refer to source code comments.

---
