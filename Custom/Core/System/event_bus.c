/**
 * @file event_bus.c
 * @brief Event Bus System Implementation
 * @version 1.0.0
 */

#include "event_bus.h"
#include "cmsis_os2.h"
#include "mem.h"
#include <string.h>
#include <stdlib.h>

/* ==================== Private Data Structure ==================== */

/**
 * @brief Event bus control block
 */
typedef struct {
    osMessageQueueId_t event_queue;               // Event queue
    osThreadId_t dispatcher_task;                 // Dispatcher task handle
    osMutexId_t registry_mutex;                   // Registry mutex
    subscriber_node_t *subscription_registry[EVENT_BUS_MAX_EVENTS]; // Subscription registry
    uint32_t next_handle;                         // Next handle value
    bool is_initialized;                          // Initialized flag
    
    // Statistics information
    uint32_t total_events_published;             // Total published events
    uint32_t total_events_processed;             // Total processed events
    uint32_t max_queue_usage;                    // Max queue usage
    uint32_t dropped_events;                     // Dropped events
} event_bus_control_t;

/* ==================== Private Variables ==================== */

static event_bus_control_t g_event_bus = {0};

/* ==================== Event Name Mapping Table ==================== */

typedef struct {
    event_id_e event_id;
    const char *name;
} event_name_map_t;

static const event_name_map_t event_name_map[] = {
    // System lifecycle events
    {EVENT_SYSTEM_STARTUP, "SYSTEM_STARTUP"},
    {EVENT_SYSTEM_SHUTDOWN, "SYSTEM_SHUTDOWN"},
    {EVENT_SYSTEM_RESET, "SYSTEM_RESET"},
    {EVENT_SYSTEM_SLEEP, "SYSTEM_SLEEP"},
    {EVENT_SYSTEM_WAKEUP, "SYSTEM_WAKEUP"},
    {EVENT_SYSTEM_ERROR, "SYSTEM_ERROR"},
    {EVENT_SYSTEM_RECOVERY, "SYSTEM_RECOVERY"},
    
    // Camera related events
    {EVENT_CAMERA_CONNECTED, "CAMERA_CONNECTED"},
    {EVENT_CAMERA_DISCONNECTED, "CAMERA_DISCONNECTED"},
    {EVENT_CAMERA_FRAME_READY, "CAMERA_FRAME_READY"},
    {EVENT_CAMERA_CAPTURE_START, "CAMERA_CAPTURE_START"},
    {EVENT_CAMERA_CAPTURE_STOP, "CAMERA_CAPTURE_STOP"},
    {EVENT_CAMERA_PARAM_CHANGED, "CAMERA_PARAM_CHANGED"},
    {EVENT_CAMERA_ERROR, "CAMERA_ERROR"},
    
    // AI inference related events
    {EVENT_AI_INFERENCE_START, "AI_INFERENCE_START"},
    {EVENT_AI_INFERENCE_COMPLETE, "AI_INFERENCE_COMPLETE"},
    {EVENT_AI_DETECTION_RESULT, "AI_DETECTION_RESULT"},
    {EVENT_AI_MODEL_LOADED, "AI_MODEL_LOADED"},
    {EVENT_AI_MODEL_SWITCHED, "AI_MODEL_SWITCHED"},
    {EVENT_AI_ERROR, "AI_ERROR"},
    
    // Network communication events
    {EVENT_NETWORK_CONNECTED, "NETWORK_CONNECTED"},
    {EVENT_NETWORK_DISCONNECTED, "NETWORK_DISCONNECTED"},
    {EVENT_NETWORK_IP_ASSIGNED, "NETWORK_IP_ASSIGNED"},
    {EVENT_WIFI_SCAN_COMPLETE, "WIFI_SCAN_COMPLETE"},
    {EVENT_MQTT_CONNECTED, "MQTT_CONNECTED"},
    {EVENT_MQTT_DISCONNECTED, "MQTT_DISCONNECTED"},
    {EVENT_MQTT_MESSAGE_RECEIVED, "MQTT_MESSAGE_RECEIVED"},
    
    // Web service events
    {EVENT_WEB_CLIENT_CONNECTED, "WEB_CLIENT_CONNECTED"},
    {EVENT_WEB_CLIENT_DISCONNECTED, "WEB_CLIENT_DISCONNECTED"},
    {EVENT_WEB_API_REQUEST, "WEB_API_REQUEST"},
    {EVENT_WEBSOCKET_CONNECTED, "WEBSOCKET_CONNECTED"},
    {EVENT_WEBSOCKET_DISCONNECTED, "WEBSOCKET_DISCONNECTED"},
    
    // Storage related events
    {EVENT_STORAGE_MOUNTED, "STORAGE_MOUNTED"},
    {EVENT_STORAGE_UNMOUNTED, "STORAGE_UNMOUNTED"},
    {EVENT_STORAGE_FULL, "STORAGE_FULL"},
    {EVENT_STORAGE_ERROR, "STORAGE_ERROR"},
    {EVENT_FILE_CREATED, "FILE_CREATED"},
    {EVENT_FILE_DELETED, "FILE_DELETED"},
    
    // Configuration management events
    {EVENT_CONFIG_UPDATED, "CONFIG_UPDATED"},
    {EVENT_CONFIG_SAVED, "CONFIG_SAVED"},
    {EVENT_CONFIG_LOADED, "CONFIG_LOADED"},
    {EVENT_CONFIG_RESET, "CONFIG_RESET"},
    {EVENT_CONFIG_IMPORT, "CONFIG_IMPORT"},
    {EVENT_CONFIG_EXPORT, "CONFIG_EXPORT"},
    
    // OTA upgrade events
    {EVENT_OTA_UPDATE_START, "OTA_UPDATE_START"},
    {EVENT_OTA_UPDATE_PROGRESS, "OTA_UPDATE_PROGRESS"},
    {EVENT_OTA_UPDATE_COMPLETE, "OTA_UPDATE_COMPLETE"},
    {EVENT_OTA_UPDATE_FAILED, "OTA_UPDATE_FAILED"},
    {EVENT_OTA_ROLLBACK, "OTA_ROLLBACK"},
    
    // Sensor events
    {EVENT_PIR_TRIGGERED, "PIR_TRIGGERED"},
    {EVENT_BUTTON_PRESSED, "BUTTON_PRESSED"},
    {EVENT_BUTTON_RELEASED, "BUTTON_RELEASED"},
    {EVENT_TEMPERATURE_ALERT, "TEMPERATURE_ALERT"},
    
    // Power management events
    {EVENT_POWER_LOW, "POWER_LOW"},
    {EVENT_POWER_CRITICAL, "POWER_CRITICAL"},
    {EVENT_POWER_RESTORED, "POWER_RESTORED"},
    {EVENT_BATTERY_LOW, "BATTERY_LOW"},
};

/* ==================== Private Function Declarations ==================== */

static void event_bus_dispatcher_task(void *argument);
static uint32_t event_id_to_index(event_id_e event_id);
static aicam_result_t add_subscriber(event_id_e event_id, 
                                     event_callback_t callback,
                                     void *context,
                                     event_filter_t *filter,
                                     event_handle_t *handle);
static aicam_result_t remove_subscriber(event_handle_t handle);
static void dispatch_event_to_subscribers(const event_t *event);
static bool apply_event_filter(const event_filter_t *filter, 
                              event_id_e event_id, 
                              const void *payload);
static void free_event_payload(event_t *event);

/* ==================== Public Function Implementations ==================== */

aicam_result_t event_bus_init(void)
{
    if (g_event_bus.is_initialized) {
        return AICAM_OK;
    }
    
    // Create event queue
    g_event_bus.event_queue = osMessageQueueNew(EVENT_BUS_QUEUE_LENGTH, sizeof(event_t), NULL);
    if (g_event_bus.event_queue == NULL) {
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Create registry mutex
    g_event_bus.registry_mutex = osMutexNew(NULL);
    if (g_event_bus.registry_mutex == NULL) {
        osMessageQueueDelete(g_event_bus.event_queue);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize subscription registry
    memset(g_event_bus.subscription_registry, 0, sizeof(g_event_bus.subscription_registry));
    
    // Create event dispatcher task
    osThreadAttr_t task_attr = {
        .name = "EventBusDispatcher",
        .stack_size = EVENT_BUS_DISPATCHER_TASK_STACK_SIZE,
        .priority = EVENT_BUS_DISPATCHER_TASK_PRIORITY
    };
    
    g_event_bus.dispatcher_task = osThreadNew(event_bus_dispatcher_task, NULL, &task_attr);
    if (g_event_bus.dispatcher_task == NULL) {
        osMutexDelete(g_event_bus.registry_mutex);
        osMessageQueueDelete(g_event_bus.event_queue);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize statistics information
    g_event_bus.next_handle = 1;
    g_event_bus.total_events_published = 0;
    g_event_bus.total_events_processed = 0;
    g_event_bus.max_queue_usage = 0;
    g_event_bus.dropped_events = 0;
    g_event_bus.is_initialized = true;
    
    return AICAM_OK;
}

aicam_result_t event_bus_deinit(void)
{
    if (!g_event_bus.is_initialized) {
        return AICAM_OK;
    }
    
    // Delete dispatcher task
    if (g_event_bus.dispatcher_task != NULL) {
        osThreadTerminate(g_event_bus.dispatcher_task);
        g_event_bus.dispatcher_task = NULL;
    }
    
    // Clear all pending events
    event_t temp_event;
    while (osMessageQueueGet(g_event_bus.event_queue, &temp_event, NULL, 0) == osOK) {
        free_event_payload(&temp_event);
    }
    
    // Delete event queue
    if (g_event_bus.event_queue != NULL) {
        osMessageQueueDelete(g_event_bus.event_queue);
        g_event_bus.event_queue = NULL;
    }
    
    // Clean up subscription registry
    if (osMutexAcquire(g_event_bus.registry_mutex, osWaitForever) == osOK) {
        for (int i = 0; i < EVENT_BUS_MAX_EVENTS; i++) {
            subscriber_node_t *node = g_event_bus.subscription_registry[i];
            while (node != NULL) {
                subscriber_node_t *next = node->next;
                hal_mem_free(node);
                node = next;
            }
            g_event_bus.subscription_registry[i] = NULL;
        }
        osMutexRelease(g_event_bus.registry_mutex);
    }
    
    // Delete registry mutex
    if (g_event_bus.registry_mutex != NULL) {
        osMutexDelete(g_event_bus.registry_mutex);
        g_event_bus.registry_mutex = NULL;
    }
    
    g_event_bus.is_initialized = false;
    return AICAM_OK;
}

event_handle_t event_bus_subscribe(event_id_e event_id,
                                   event_callback_t callback,
                                   void *context,
                                   event_filter_t *filter)
{
    if (!g_event_bus.is_initialized || callback == NULL) {
        return 0;
    }
    
    event_handle_t handle = 0;
    aicam_result_t status = add_subscriber(event_id, callback, context, filter, &handle);
    
    return (status == AICAM_OK) ? handle : 0;
}

aicam_result_t event_bus_unsubscribe(event_handle_t handle)
{
    if (!g_event_bus.is_initialized || handle == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    return remove_subscriber(handle);
}

aicam_result_t event_bus_publish(event_id_e event_id, 
                                 const void *payload, 
                                 uint16_t payload_size,
                                 aicam_priority_t priority)
{
    if (!g_event_bus.is_initialized) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Create event
    event_t event = {
        .event_id = event_id,
        .timestamp = osKernelGetTickCount(),
        .payload_size = payload_size,
        .priority = priority,
        .context = NULL
    };
    
    // Copy payload data
    if (payload != NULL && payload_size > 0) {
        event.payload = hal_mem_alloc_fast(payload_size);
        if (event.payload == NULL) {
            return AICAM_ERROR_NO_MEMORY;
        }
        memcpy(event.payload, payload, payload_size);
    } else {
        event.payload = NULL;
    }
    
    // Send to queue
    osStatus_t result = osMessageQueuePut(g_event_bus.event_queue, &event, 0, 10);
    if (result != osOK) {
        // Queue full, release payload memory and increase dropped count
        if (event.payload != NULL) {
            hal_mem_free(event.payload);
        }
        g_event_bus.dropped_events++;
        return AICAM_ERROR_FULL;
    }
    
    g_event_bus.total_events_published++;
    
    // Update max queue usage statistics
    uint32_t queue_usage = osMessageQueueGetCount(g_event_bus.event_queue);
    if (queue_usage > g_event_bus.max_queue_usage) {
        g_event_bus.max_queue_usage = queue_usage;
    }
    
    return AICAM_OK;
}

aicam_result_t event_bus_publish_from_isr(event_id_e event_id,
                                          const void *payload,
                                          uint16_t payload_size,
                                          aicam_priority_t priority)
{
    if (!g_event_bus.is_initialized) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Cannot dynamically allocate memory in ISR, so payload must be NULL or static data
    if (payload != NULL && payload_size > 0) {
        return AICAM_ERROR_NOT_SUPPORTED;
    }
    
    // Create event
    event_t event = {
        .event_id = event_id,
        .timestamp = osKernelGetTickCount(),
        .payload = NULL,
        .payload_size = 0,
        .priority = priority,
        .context = NULL
    };
    
    // Send to queue
    osStatus_t result = osMessageQueuePut(g_event_bus.event_queue, &event, 0, 0);
    if (result != osOK) {
        g_event_bus.dropped_events++;
        return AICAM_ERROR_FULL;
    }
    
    g_event_bus.total_events_published++;
    
    return AICAM_OK;
}

aicam_result_t event_bus_get_stats(uint32_t *total_events,
                                   uint32_t *pending_events,
                                   uint32_t *max_queue_usage)
{
    if (!g_event_bus.is_initialized) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    if (total_events != NULL) {
        *total_events = g_event_bus.total_events_processed;
    }
    
    if (pending_events != NULL) {
        *pending_events = osMessageQueueGetCount(g_event_bus.event_queue);
    }
    
    if (max_queue_usage != NULL) {
        *max_queue_usage = g_event_bus.max_queue_usage;
    }
    
    return AICAM_OK;
}

aicam_result_t event_bus_flush(void)
{
    if (!g_event_bus.is_initialized) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    event_t event;
    while (osMessageQueueGet(g_event_bus.event_queue, &event, NULL, 0) == osOK) {
        free_event_payload(&event);
    }
    
    return AICAM_OK;
}

const char* event_bus_get_event_name(event_id_e event_id)
{
    for (size_t i = 0; i < AICAM_ARRAY_SIZE(event_name_map); i++) {
        if (event_name_map[i].event_id == event_id) {
            return event_name_map[i].name;
        }
    }
    
    return "UNKNOWN_EVENT";
}

/* ==================== Private Function Implementations ==================== */

static void event_bus_dispatcher_task(void *argument)
{
    AICAM_UNUSED(argument);
    
    event_t event;
    
    while (1) {
        // Wait for event
        if (osMessageQueueGet(g_event_bus.event_queue, &event, NULL, osWaitForever) == osOK) {
            // Dispatch event to all subscribers
            dispatch_event_to_subscribers(&event);
            
            // Release event payload memory
            free_event_payload(&event);
            
            g_event_bus.total_events_processed++;
        }
    }
}

static uint32_t event_id_to_index(event_id_e event_id)
{
    return ((uint32_t)event_id) % EVENT_BUS_MAX_EVENTS;
}

static aicam_result_t add_subscriber(event_id_e event_id, 
                                     event_callback_t callback,
                                     void *context,
                                     event_filter_t *filter,
                                     event_handle_t *handle)
{
    if (osMutexAcquire(g_event_bus.registry_mutex, 100) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    // Allocate new subscriber node
    subscriber_node_t *new_node = hal_mem_alloc_fast(sizeof(subscriber_node_t));
    if (new_node == NULL) {
        osMutexRelease(g_event_bus.registry_mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Initialize subscriber node
    new_node->callback = callback;
    new_node->context = context;
    new_node->filter = filter;
    new_node->next = NULL;
    
    // Get event index
    uint32_t index = event_id_to_index(event_id);
    
    // Insert into linked list head
    new_node->next = g_event_bus.subscription_registry[index];
    g_event_bus.subscription_registry[index] = new_node;
    
    // Generate handle (use node address as handle)
    *handle = (event_handle_t)new_node;
    
    osMutexRelease(g_event_bus.registry_mutex);
    
    return AICAM_OK;
}

static aicam_result_t remove_subscriber(event_handle_t handle)
{
    if (osMutexAcquire(g_event_bus.registry_mutex, 100) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    subscriber_node_t *target_node = (subscriber_node_t *)handle;
    bool found = false;
    
    // Find and remove target node in all linked lists
    for (uint32_t i = 0; i < EVENT_BUS_MAX_EVENTS; i++) {
        subscriber_node_t **current = &g_event_bus.subscription_registry[i];
        
        while (*current != NULL) {
            if (*current == target_node) {
                *current = target_node->next;
                hal_mem_free(target_node);
                found = true;
                break;
            }
            current = &((*current)->next);
        }
        
        if (found) {
            break;
        }
    }
    
    osMutexRelease(g_event_bus.registry_mutex);
    
    return found ? AICAM_OK : AICAM_ERROR_NOT_FOUND;
}

static void dispatch_event_to_subscribers(const event_t *event)
{
    if (osMutexAcquire(g_event_bus.registry_mutex, 100) != osOK) {
        return;
    }
    
    uint32_t index = event_id_to_index(event->event_id);
    subscriber_node_t *node = g_event_bus.subscription_registry[index];
    
    while (node != NULL) {
        // Apply event filter
        bool should_dispatch = true;
        if (node->filter != NULL) {
            should_dispatch = apply_event_filter(node->filter, event->event_id, event->payload);
        }
        
        // Call subscriber callback
        if (should_dispatch && node->callback != NULL) {
            node->callback(event);
        }
        
        node = node->next;
    }
    
    osMutexRelease(g_event_bus.registry_mutex);
}

static bool apply_event_filter(const event_filter_t *filter, 
                              event_id_e event_id, 
                              const void *payload)
{
    if (filter == NULL || filter->filter_func == NULL) {
        return true;
    }
    
    return filter->filter_func(event_id, payload);
}

static void free_event_payload(event_t *event)
{
    if (event->payload != NULL) {
        hal_mem_free(event->payload);
        event->payload = NULL;
    }
}
