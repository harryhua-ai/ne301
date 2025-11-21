#ifndef GENERIC_LED_H
#define GENERIC_LED_H

#include <stdint.h>
#include <stdbool.h>

// LED state enumeration
typedef enum {
    LED_STATE_OFF,      // Always off
    LED_STATE_ON,       // Always on
    LED_STATE_BLINK     // Blinking
} led_state;

// LED operation callback function type definition
typedef void (*LedOpFunc)(void);           // LED on/off operation function
typedef void (*LockFunc)(bool lock);       // Thread lock function (true=lock, false=unlock)
typedef uint32_t (*GetTickFunc)(void);     // Get current timestamp function (milliseconds)

// LED control structure
typedef struct {
    // User configuration
    LedOpFunc turn_on;          // Function to turn on LED
    LedOpFunc turn_off;         // Function to turn off LED
    LockFunc lock;              // Thread-safe lock function
    GetTickFunc get_tick;       // Get timestamp function
    
    // Control state
    led_state state;             // Current state
    uint32_t blink_interval;    // Blink interval (ms)
    uint32_t blink_count;       // Blink count (0=infinite)
    uint32_t blink_counter;     // Current blink counter
    bool blink_phase;           // Blink phase (true=on, false=off)
    bool is_active;             // Whether LED is active
    
    // Time management
    uint32_t last_toggle_time;  // Last state toggle timestamp
} led_handle;

// Module global variables
#define MAX_LEDS 5  // Maximum supported LED count

void led_module_init(void);
int led_register(LedOpFunc on_func, LedOpFunc off_func, LockFunc lock_func, GetTickFunc tick_func);
void led_set_state(int handle, led_state state, uint32_t blink_times, uint32_t interval_ms) ;
void led_service(void);
#endif