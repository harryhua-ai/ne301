#include <stddef.h>
#include <stdio.h>
#include "generic_key.h"

void key_module_init(key_instance_t *key) 
{

    key->state = KEY_STATE_RELEASE;
}


void key_regitster_cb(key_instance_t *key, key_event event, key_cb_t cb)
{
    if (key == NULL || cb == NULL) return;
    switch (event){
        case KEY_EVENT_SHORT_PRESS:
            key->config.short_press_cb = cb;
            break;
        case KEY_EVENT_DOUBLE_CLICK:
            key->config.double_click_cb = cb;
            break;

        case KEY_EVENT_LONG_PRESS:
            key->config.long_press_cb = cb;
            break;

        case KEY_EVENT_SUPER_LONG_PRESS:
            key->config.super_long_press_cb = cb;
            break;

        default:
            break;
    }
}

/**
 * @brief Key state machine processing function (needs to be called periodically)
 * @param key Key instance pointer
 * @param elapsed_ms Elapsed time since last call (ms)
 */
void key_process(key_instance_t *key, uint32_t elapsed_ms) 
{
    if(key == NULL || key->config.read_key_state == NULL)
        return;
    uint8_t current_state = key->config.read_key_state();
    uint32_t press_duration;

    switch (key->state) {
        case KEY_STATE_RELEASE:
            if (current_state) {  // Press detected
                key->state = KEY_STATE_PRESS_DETECT;
                key->press_timestamp = 0;  // Start debounce timer
            }
            break;
            
        case KEY_STATE_PRESS_DETECT:
            key->press_timestamp += elapsed_ms;
            if (key->press_timestamp >= key->config.debounce_time) {
                if (current_state) {  // Press confirmed
                    key->state = KEY_STATE_PRESS;
                    key->press_timestamp = 0;  // Reset for long press timing
                } else {  // Bounce, return to release state
                    key->state = KEY_STATE_RELEASE;
                }
            }
            break;
            
        case KEY_STATE_PRESS:
            press_duration = key->press_timestamp + elapsed_ms;
            
            // Detect long press event
            if (press_duration >= key->config.long_press_time) {
                key->event = KEY_EVENT_LONG_PRESS;
                key->state = KEY_STATE_LONG_PRESS;
                printf("key_process: LONG_PRESS\r\n");
                if (key->config.long_press_cb) {
                    key->config.long_press_cb();
                }
                // Continue timing for super long press detection
                key->press_timestamp = key->config.long_press_time;
            }
            // Detect release
            else if (!current_state) {
                key->state = KEY_STATE_WAIT_DOUBLE;
                key->release_timestamp = 0;  // Start double click wait timer
            }
            // Update time
            else {
                key->press_timestamp = press_duration;
            }
            break;
            
        case KEY_STATE_LONG_PRESS:
            press_duration = key->press_timestamp + elapsed_ms;
            if (press_duration >= key->config.super_long_press_time) {
                key->event = KEY_EVENT_SUPER_LONG_PRESS;
                key->state = KEY_STATE_SUPER_LONG_PRESS;
                printf("key_process: SUPER_LONG_PRESS\r\n");
                if (key->config.super_long_press_cb) {
                    key->config.super_long_press_cb();
                }
            }
            // Detect release
            else if (!current_state) {
                key->state = KEY_STATE_RELEASE;
            }else {
                key->press_timestamp = press_duration;
            }
            break;
            
        case KEY_STATE_WAIT_DOUBLE:
            key->release_timestamp += elapsed_ms;
            
            // Double click timeout, trigger short press
            if (key->release_timestamp >= key->config.double_click_time) {
                key->event = KEY_EVENT_SHORT_PRESS;
                printf("key_process: SHORT_PRESS\r\n");
                if (key->config.short_press_cb) {
                    key->config.short_press_cb();
                }
                key->state = KEY_STATE_RELEASE;
            }
            // Detect second press
            else if (current_state) {
                key->state = KEY_STATE_DOUBLE_PRESS_DETECT;
                key->press_timestamp = 0;  // Start debounce timer
            }
            break;
            
        case KEY_STATE_DOUBLE_PRESS_DETECT:
            key->press_timestamp += elapsed_ms;
            if (key->press_timestamp >= key->config.debounce_time) {
                if (current_state) {  // Second press confirmed
                    key->state = KEY_STATE_DOUBLE_PRESS;
                    key->press_timestamp = 0;  // Reset timer
                } else {  // Bounce, return to wait state
                    key->state = KEY_STATE_WAIT_DOUBLE;
                }
            }
            break;
            
        case KEY_STATE_DOUBLE_PRESS:
            if (!current_state) {  // Detect release
                key->event = KEY_EVENT_DOUBLE_CLICK;
                printf("key_process: DOUBLE_CLICK\r\n");
                if (key->config.double_click_cb) {
                    key->config.double_click_cb();
                }
                key->state = KEY_STATE_RELEASE;
            }
            break;
            
        case KEY_STATE_SUPER_LONG_PRESS:
            if (!current_state) {  // Return to initial state after release
                key->state = KEY_STATE_RELEASE;
            }
            break;
    }
}