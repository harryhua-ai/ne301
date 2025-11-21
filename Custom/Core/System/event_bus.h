/**
 * @file event_bus.h
 * @brief Event Bus System Header File
 * @version 1.0.0
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "aicam_types.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Event ID Definitions ==================== */

/**
 * @brief System Global Event and Command ID Definitions
 */
typedef enum {
    // System lifecycle events (0x0000-0x00FF)
    EVENT_SYSTEM_STARTUP = 0x0001,          // System startup complete
    EVENT_SYSTEM_SHUTDOWN = 0x0002,         // System shutdown
    EVENT_SYSTEM_RESET = 0x0003,            // System reset
    EVENT_SYSTEM_SLEEP = 0x0004,            // System enter sleep
    EVENT_SYSTEM_WAKEUP = 0x0005,           // System wakeup
    EVENT_SYSTEM_ERROR = 0x0006,            // System error
    EVENT_SYSTEM_RECOVERY = 0x0007,         // System recovery
    
    // Camera related events (0x0100-0x01FF)
    EVENT_CAMERA_CONNECTED = 0x0101,        // Camera connected
    EVENT_CAMERA_DISCONNECTED = 0x0102,     // Camera disconnected
    EVENT_CAMERA_FRAME_READY = 0x0103,      // New frame ready
    EVENT_CAMERA_CAPTURE_START = 0x0104,    // Start capture
    EVENT_CAMERA_CAPTURE_STOP = 0x0105,     // Stop capture
    EVENT_CAMERA_PARAM_CHANGED = 0x0106,    // Camera parameter changed
    EVENT_CAMERA_ERROR = 0x0107,            // Camera error
    
    // AI inference related events (0x0200-0x02FF)
    EVENT_AI_INFERENCE_START = 0x0201,      // AI inference start
    EVENT_AI_INFERENCE_COMPLETE = 0x0202,   // AI inference complete
    EVENT_AI_DETECTION_RESULT = 0x0203,     // Detection result
    EVENT_AI_MODEL_LOADED = 0x0204,         // Model loaded
    EVENT_AI_MODEL_SWITCHED = 0x0205,       // Model switched
    EVENT_AI_ERROR = 0x0206,                // AI inference error
    
    // Network communication events (0x0300-0x03FF)
    EVENT_NETWORK_CONNECTED = 0x0301,       // Network connected
    EVENT_NETWORK_DISCONNECTED = 0x0302,    // Network disconnected
    EVENT_NETWORK_IP_ASSIGNED = 0x0303,     // IP address assigned
    EVENT_WIFI_SCAN_COMPLETE = 0x0304,      // WiFi scan complete
    EVENT_MQTT_CONNECTED = 0x0305,          // MQTT connected
    EVENT_MQTT_DISCONNECTED = 0x0306,       // MQTT disconnected
    EVENT_MQTT_MESSAGE_RECEIVED = 0x0307,   // MQTT message received
    
    // Web service events (0x0400-0x04FF)
    EVENT_WEB_CLIENT_CONNECTED = 0x0401,    // Web client connected
    EVENT_WEB_CLIENT_DISCONNECTED = 0x0402, // Web client disconnected
    EVENT_WEB_API_REQUEST = 0x0403,         // Web API request
    EVENT_WEBSOCKET_CONNECTED = 0x0404,     // WebSocket connected
    EVENT_WEBSOCKET_DISCONNECTED = 0x0405,  // WebSocket disconnected
    
    // Storage related events (0x0500-0x05FF)
    EVENT_STORAGE_MOUNTED = 0x0501,         // Storage device mounted
    EVENT_STORAGE_UNMOUNTED = 0x0502,       // Storage device unmounted
    EVENT_STORAGE_FULL = 0x0503,            // Storage space full
    EVENT_STORAGE_ERROR = 0x0504,           // Storage error
    EVENT_FILE_CREATED = 0x0505,            // File created
    EVENT_FILE_DELETED = 0x0506,            // File deleted
    
    // Configuration management events (0x0600-0x06FF)
    EVENT_CONFIG_UPDATED = 0x0601,          // Configuration updated
    EVENT_CONFIG_SAVED = 0x0602,            // Configuration saved
    EVENT_CONFIG_LOADED = 0x0603,           // Configuration loaded
    EVENT_CONFIG_RESET = 0x0604,            // Configuration reset
    EVENT_CONFIG_IMPORT = 0x0605,           // Configuration import
    EVENT_CONFIG_EXPORT = 0x0606,           // Configuration export
    
    // OTA upgrade events (0x0700-0x07FF)
    EVENT_OTA_UPDATE_START = 0x0701,        // OTA update start
    EVENT_OTA_UPDATE_PROGRESS = 0x0702,     // OTA update progress
    EVENT_OTA_UPDATE_COMPLETE = 0x0703,     // OTA update complete
    EVENT_OTA_UPDATE_FAILED = 0x0704,       // OTA update failed
    EVENT_OTA_ROLLBACK = 0x0705,            // OTA rollback
    
    // Sensor events (0x0800-0x08FF)
    EVENT_PIR_TRIGGERED = 0x0801,           // PIR sensor triggered
    EVENT_BUTTON_PRESSED = 0x0802,          // Button pressed
    EVENT_BUTTON_RELEASED = 0x0803,         // Button released
    EVENT_TEMPERATURE_ALERT = 0x0804,       // Temperature alert
    
    // Power management events (0x0900-0x09FF)
    EVENT_POWER_LOW = 0x0901,               // Power voltage low
    EVENT_POWER_CRITICAL = 0x0902,          // Power voltage critical
    EVENT_POWER_RESTORED = 0x0903,          // Power restored
    EVENT_BATTERY_LOW = 0x0904,             // Battery level low
    
    // User defined events (0x1000-0x1FFF)
    EVENT_USER_DEFINED_START = 0x1000,
    EVENT_USER_DEFINED_END = 0x1FFF,
    
    EVENT_MAX = 0xFFFF
} event_id_e;

#define AICAM_MAX_SSID_LENGTH 32
#define AICAM_MAX_PASSWORD_LENGTH 64
#define AICAM_MAX_MESSAGE_LENGTH 256

/* ==================== Data Structure Definitions ==================== */

/**
 * @brief Event handle type
 */
typedef uint32_t event_handle_t;


/**
 * @brief Event filter structure
 */
typedef struct {
    bool (*filter_func)(event_id_e event_id, const void *payload);
    void *filter_context;
} event_filter_t;


/**
 * @brief Event bus message structure
 */
typedef struct event_t {
    event_id_e event_id;               // Event ID
    uint32_t timestamp;                // Timestamp
    uint16_t payload_size;             // Payload size
    void *payload;                     // Event payload data
    aicam_priority_t priority;         // Event priority
    void *context;                     // Context data
} event_t;

/**
 * @brief Subscriber callback function pointer type definition
 * @param event Pointer to the received event message constant
 */
typedef void (*event_callback_t)(const struct event_t* event);

/**
 * @brief Subscriber linked list node
 */
typedef struct subscriber_node_t {
    event_callback_t callback;         // Callback function pointer
    void *context;                     // Callback context
    event_filter_t *filter;           // Event filter
    struct subscriber_node_t *next;    // Pointer to next subscriber
} subscriber_node_t;


/* ==================== Event Payload Structure Definitions ==================== */

/**
 * @brief WiFi configuration event payload
 */
typedef struct {
    char ssid[AICAM_MAX_SSID_LENGTH];
    char password[AICAM_MAX_PASSWORD_LENGTH];
} event_payload_wifi_config_t;

/**
 * @brief Battery status event payload
 */
typedef struct {
    uint8_t percentage;    // Battery percentage
    bool is_charging;      // Is charging
} event_payload_battery_status_t;

/**
 * @brief Video packet event payload
 */
typedef struct {
    void *packet;          // Pointer to encoded video packet for zero-copy
    size_t packet_size;    // Packet size
} event_payload_video_packet_t;

/**
 * @brief OTA progress event payload
 */
typedef struct {
    uint32_t total_size;   // Total size
    uint32_t downloaded;   // Downloaded size
    uint8_t progress;      // Progress percentage
} event_payload_ota_progress_t;

/**
 * @brief System error event payload
 */
typedef struct {
    uint32_t error_code;   // Error code
    char error_msg[AICAM_MAX_MESSAGE_LENGTH]; // Error message
    const char *source_file;  // Source file name
    uint32_t source_line;     // Source file line number
} event_payload_system_error_t;

/* ==================== Configuration Parameter Definitions ==================== */

#ifndef EVENT_BUS_QUEUE_LENGTH
#define EVENT_BUS_QUEUE_LENGTH 32
#endif

#ifndef EVENT_BUS_DISPATCHER_TASK_PRIORITY
#define EVENT_BUS_DISPATCHER_TASK_PRIORITY osPriorityHigh
#endif

#ifndef EVENT_BUS_DISPATCHER_TASK_STACK_SIZE
#define EVENT_BUS_DISPATCHER_TASK_STACK_SIZE 2048
#endif

#ifndef EVENT_BUS_MAX_EVENTS
#define EVENT_BUS_MAX_EVENTS 512
#endif

/* ==================== Interface Function Declarations ==================== */

/**
 * @brief Initialize event bus system
 * @note  This function creates event queue and dispatcher task, must be called before RTOS scheduler starts
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_init(void);

/**
 * @brief Deinitialize event bus system
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_deinit(void);

/**
 * @brief Subscribe to event
 * @param event_id Event ID to subscribe
 * @param callback Event callback function
 * @param context Callback context
 * @param filter Event filter (optional, pass NULL for no filtering)
 * @return event_handle_t Subscription handle, returns 0 on failure
 */
event_handle_t event_bus_subscribe(event_id_e event_id,
                                   event_callback_t callback,
                                   void *context,
                                   event_filter_t *filter);

/**
 * @brief Unsubscribe from event
 * @param handle Subscription handle
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_unsubscribe(event_handle_t handle);

/**
 * @brief Publish event to event bus
 * @param event_id Event ID
 * @param payload Event payload data (can be NULL)
 * @param payload_size Payload data size
 * @param priority Event priority
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_publish(event_id_e event_id, 
                                 const void *payload, 
                                 uint16_t payload_size,
                                 aicam_priority_t priority);

/**
 * @brief Publish event to event bus (called from ISR)
 * @param event_id Event ID
 * @param payload Event payload data (can be NULL)
 * @param payload_size Payload data size
 * @param priority Event priority
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_publish_from_isr(event_id_e event_id,
                                          const void *payload,
                                          uint16_t payload_size,
                                          aicam_priority_t priority);

/**
 * @brief Get event bus statistics
 * @param total_events Total event count (output parameter)
 * @param pending_events Pending event count (output parameter)
 * @param max_queue_usage Maximum queue usage (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_get_stats(uint32_t *total_events,
                                   uint32_t *pending_events,
                                   uint32_t *max_queue_usage);

/**
 * @brief Flush event bus queue
 * @return aicam_result_t Operation result
 */
aicam_result_t event_bus_flush(void);

/**
 * @brief Get event name string (for debugging)
 * @param event_id Event ID
 * @return const char* Event name string
 */
const char* event_bus_get_event_name(event_id_e event_id);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_H
