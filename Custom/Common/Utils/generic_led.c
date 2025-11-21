#include "generic_led.h"

static led_handle leds[MAX_LEDS];

void led_module_init(void)
{
    for (int i = 0; i < MAX_LEDS; i++) {
        leds[i].is_active = false;
    }
}

// Register LED
int led_register(LedOpFunc on_func, LedOpFunc off_func, LockFunc lock_func, GetTickFunc tick_func)
{
    for (int i = 0; i < MAX_LEDS; i++) {
        if (!leds[i].is_active) {
            leds[i].turn_on = on_func;
            leds[i].turn_off = off_func;
            leds[i].lock = lock_func;
            leds[i].get_tick = tick_func;
            leds[i].state = LED_STATE_OFF;
            leds[i].is_active = true;
            leds[i].last_toggle_time = 0;
            
            // Set initial state to off
            if (leds[i].lock) leds[i].lock(true);
            off_func();
            if (leds[i].lock) leds[i].lock(false);
            
            return i;  // Return LED handle
        }
    }
    return -1;  // Registration failed
}

// Set LED state
void led_set_state(int handle, led_state state, uint32_t blink_times, uint32_t interval_ms) 
{
    if (handle < 0 || handle >= MAX_LEDS || !leds[handle].is_active) return;
    
    led_handle *led = &leds[handle];
    
    // Thread-safe lock
    if (led->lock) led->lock(true);
    
    led->state = state;
    led->last_toggle_time = led->get_tick(); // Record setting time
    
    switch (state) {
        case LED_STATE_OFF:
            led->turn_off();
            break;
        case LED_STATE_ON:
            led->turn_on();
            break;
        case LED_STATE_BLINK:
            led->blink_interval = interval_ms;
            led->blink_count = blink_times;
            led->blink_counter = 0;
            led->blink_phase = false;
            led->turn_on();           // Start first on
            break;
    }
    
    // Unlock
    if (led->lock) led->lock(false);
}

// LED service function (needs to be called periodically in main loop)
void led_service(void) 
{
    uint32_t current_time;
    
    for (int i = 0; i < MAX_LEDS; i++) {
        if (!leds[i].is_active) 
            continue;
            
        led_handle *led = &leds[i];
        
        // Non-blinking state needs no processing
        if (led->state != LED_STATE_BLINK)
            continue;
        
        // Get current time
        current_time = led->get_tick();
        // Check if toggle time reached
        if ((current_time - led->last_toggle_time) < led->blink_interval)
            continue;
        
        // Thread-safe lock
        if (led->lock) led->lock(true);
        
        // Update toggle time
        led->last_toggle_time = current_time;
        
        // Toggle phase
        if (led->blink_phase) {
            led->turn_on();
        } else {
            led->turn_off();
            if (led->blink_count > 0) {
                led->blink_counter++;
                if (led->blink_counter >= led->blink_count) {
                    led->state = LED_STATE_OFF;  // Blinking complete, turn to always off
                    led->turn_off();
                    led->blink_phase = true; // Reset phase
                    if (led->lock) led->lock(false);
                    continue;
                }
            }
        }
        led->blink_phase = !led->blink_phase;
        
        // Unlock
        if (led->lock) led->lock(false);
    }
}