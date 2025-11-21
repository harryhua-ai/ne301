#ifndef GENERIC_KEY_H
#define GENERIC_KEY_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*key_cb_t)(void);

typedef enum {
    KEY_STATE_RELEASE = 0,          // Release state
    KEY_STATE_PRESS_DETECT,         // Press detection (debounce)
    KEY_STATE_PRESS,                // Press confirmed
    KEY_STATE_WAIT_DOUBLE,          // Wait for double click
    KEY_STATE_DOUBLE_PRESS_DETECT,  // Double press detection
    KEY_STATE_DOUBLE_PRESS,         // Double press confirmed
    KEY_STATE_LONG_PRESS,           // Long press
    KEY_STATE_SUPER_LONG_PRESS      // Super long press
} key_state;

// Key event enumeration
typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT_PRESS,          // Short press event
    KEY_EVENT_DOUBLE_CLICK,         // Double click event
    KEY_EVENT_LONG_PRESS,           // Long press event
    KEY_EVENT_SUPER_LONG_PRESS      // Super long press event
} key_event;

// Key configuration structure
typedef struct {
    // Externally registered callback function
    uint8_t (*read_key_state)(void);// Read key state function (0:release, 1:press)
    
    // Time threshold configuration (unit: ms)
    uint32_t debounce_time;         // Debounce time
    uint32_t double_click_time;     // Double click interval time
    uint32_t long_press_time;       // Long press time
    uint32_t super_long_press_time; // Super long press time
    
    // Event callback functions
    void (*short_press_cb)(void);
    void (*double_click_cb)(void);
    void (*long_press_cb)(void);
    void (*super_long_press_cb)(void);
} key_config_t;

// Key instance structure
typedef struct {
    key_state state;                 // Current state
    key_event event;                 // Detected event
    uint32_t press_timestamp;       // Press timestamp
    uint32_t release_timestamp;     // Release timestamp
    key_config_t config;        // Configuration pointer
} key_instance_t;

void key_module_init(key_instance_t *key);
void key_regitster_cb(key_instance_t *key, key_event event, key_cb_t cb);
void key_process(key_instance_t *key, uint32_t elapsed_ms);

#endif