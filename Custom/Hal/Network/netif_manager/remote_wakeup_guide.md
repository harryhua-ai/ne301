
# USB ECM Network Interface Driver - Usage & Troubleshooting Guide

## Overview
This document provides guidance on using the USB ECM (Ethernet Control Model) network interface driver and troubleshooting common issues.

## Quick Start

### Initialize USB ECM Interface
```bash
ifconfig ue init
```

Expected output sequence:
1. `Waiting for USB ECM activate event...`
2. Device messages from `usb_host_ecm.c`:
   - `USB ECM Event:0x1` (DEVICE_INSERTION)
   - `USB ECM Inserted`
   - `activate, mac = XX:XX:XX:XX:XX:XX`
   - `USB ECM Event:0x81` (DEVICE_CONNECTION with EVENT_ACTIVATE)
3. Upon success: `USB ECM netif init success`

### Check Interface Status
```bash
ifconfig ue info
ifconfig ue state
```

### Deinitialize
```bash
ifconfig ue deinit
```

---

## Troubleshooting

### Issue: Stuck/Hangs During Init

**Symptom:**
```
Waiting for USB ECM activate event...
(hangs for 30 seconds then times out)
```

**Root Cause:**
The activate callback event was not being set because `usb_ecm_events` was NULL at callback time.

**Root Reason:**
Event flags were created AFTER `usb_host_ecm_init()`, but USBX ECM driver calls the activate callback immediately or synchronously during init.

**Solution (Applied):**
1. Event flags and mutex are now created BEFORE `usb_host_ecm_init()`
2. Activate event flag is cleared before USB init
3. Callbacks can now safely signal events

**Verification:**
Check debug logs:
- `usb_ecm_on_activate called, events=0x<valid address>, mac=0x<valid>`
- `MAC copied: 22:89:84:6a:96:ab` (or your device MAC)
- `Setting USB_ECM_EVENT_ACTIVATE flag`
- `USB_ECM_EVENT_ACTIVATE flag set`
- `USB ECM wait result: 0x4` (bit 2 = USB_ECM_EVENT_ACTIVATE)

If you see `usb_ecm_events is NULL!`, initialization order is wrong.

### Issue: Activate Event Timeout

**Symptom:**
```
USB ECM activate event timeout/error!
USB ECM netif init failed: AICAM_ERROR_TIMEOUT
```

**Possible Causes:**
1. **Device not detected**: USB cable not connected or device not recognized
2. **CAT1 ECM mode not enabled**: If `NETIF_USB_ECM_IS_CAT1_MODULE=1`, CAT1 module must support ECM
3. **USBX stack issue**: HCD initialization failed

**Debugging:**
- Check `usb_host_ecm_init` return value (should be 0)
- Check for USB enumeration errors in early boot logs
- Verify CAT1 ECM enable command succeeds (if applicable)

### Issue: Link Up Event Not Triggering

**Symptom:**
- Device activates successfully
- But `ifconfig ue info` shows state as DOWN
- No DHCP address assigned

**Possible Causes:**
1. **Link state not changing**: Device not sending link-up notification
2. **Callback not wired**: link_up callback not triggered

**Check:**
- Enable more verbose logging in `usb_host_ecm` callbacks
- Verify `usb_ecm_on_link_up` is called in debug output

---

## Architecture

### Event Flags
- `USB_ECM_EVENT_ACTIVATE` (bit 2): Device activated, MAC address obtained
- `USB_ECM_EVENT_LINK_UP` (bit 0): Network link is up
- `USB_ECM_EVENT_LINK_DOWN` (bit 1): Network link is down

### Control Flow
```
Init Command
  ↓
Create Events/Mutex
  ↓
Register lwIP netif
  ↓
Enable CAT1 ECM (if needed)
  ↓
Initialize USB ECM Host Stack
  ↓
  USB Device Insertion
    ↓
    Activate Callback Fires
      ↓
      Set ACTIVATE Event
  ↓
Wait for ACTIVATE Event (30sec timeout)
  ↓
Success: Interface ready, awaiting link-up
  ↓
Link Up Event
  ↓
Interface UP + DHCP Start
  ↓
IP Address Assigned
```

### Event-Driven vs Manual Control
- **Previous (incorrect)**: User had to manually call `ifconfig ue up`
- **Current (correct)**: Link state changes are automatic via callbacks
  - Manual UP/DOWN return `AICAM_ERROR_NOT_SUPPORTED`
  - Deactivate event automatically brings interface down

---

## Configuration (netif_manager.h)

```c
#define NETIF_USB_ECM_DEFAULT_DHCP_TIMEOUT  (30000)  // 30sec init timeout
#define NETIF_USB_ECM_DEFAULT_IP_MODE       (NETIF_IP_MODE_DHCP)  // Auto-assign
#define NETIF_USB_ECM_IS_CAT1_MODULE        (1)      // Enable CAT1 ECM mode
```

---

## Testing Checklist

- [ ] Device plugged in before init
- [ ] `ifconfig ue init` completes within 30 seconds
- [ ] `ifconfig ue info` shows MAC address
- [ ] DHCP client obtains IP address
- [ ] Ping external host succeeds
- [ ] `ifconfig ue deinit` cleans up without errors

---

## Debug Logging

All debug output uses `LOG_DRV_INFO/LOG_DRV_ERROR` macros. Enable driver logging to see:
- Callback invocations
- Event flag operations
- Network interface state changes
- DHCP lease events

Enable with (typically in Makefile or project settings):
```c
#define LOG_DRV_LEVEL   LOG_LEVEL_DEBUG
```

---

## See Also
- `usb_ecm_netif.c` - Core driver implementation
- `usb_host_ecm.[ch]` - USB host ECM class driver
- `netif_manager.c` - Network interface dispatcher
- `cat1.c` - CAT1 cellular module integration
