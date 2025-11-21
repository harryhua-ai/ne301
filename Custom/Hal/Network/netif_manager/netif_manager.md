
---

# Network Interface Management Module Interface Documentation

## 1. Overview

`netif_manager.h` provides a unified network interface management interface, supporting initialization, configuration, state management, wireless scanning and other operations for various types of network cards such as local, wireless, Ethernet, and 4G. Suitable for embedded network devices based on the lwIP protocol stack.

---

## 2. Type Definitions

### 2.1 Network Interface Type `netif_type_t`
- `NETIF_TYPE_LOCAL`: Local loopback network interface
- `NETIF_TYPE_WIRELESS`: Wireless network interface (e.g., Wi-Fi)
- `NETIF_TYPE_ETH`: Ethernet network interface
- `NETIF_TYPE_4G`: 4G network interface

### 2.2 Network Interface State `netif_state_t`
- `NETIF_STATE_DEINIT`: Not initialized
- `NETIF_STATE_DOWN`: Not enabled
- `NETIF_STATE_UP`: Enabled

### 2.3 IP Mode `netif_ip_mode_t`
- `NETIF_IP_MODE_STATIC`: Static IP
- `NETIF_IP_MODE_DHCP`: DHCP client
- `NETIF_IP_MODE_DHCPS`: DHCP server

### 2.4 Network Interface Operation Command `netif_cmd_t`
- `NETIF_CMD_CFG`: Configure
- `NETIF_CMD_INIT`: Initialize
- `NETIF_CMD_UP`: Enable
- `NETIF_CMD_INFO`: Get information
- `NETIF_CMD_STATE`: Get state
- `NETIF_CMD_DOWN`: Disable
- `NETIF_CMD_UNINIT`: Destroy

### 2.5 Wireless Security Type `wireless_security_t`
See header file comments for details, covering common Wi-Fi security protocols.

### 2.6 Wireless Encryption Method `wireless_encryption_t`
See header file comments for details, covering common Wi-Fi encryption methods.

---

## 3. Structure Descriptions

### 3.1 `wireless_config_t`
Wireless configuration parameters, including SSID, password, security type, encryption method, channel, maximum number of clients, etc.

### 3.2 `wireless_scan_info_t`
Single AP information from wireless scan, including RSSI, SSID, BSSID, channel, security mode.

### 3.3 `wireless_scan_result_t`
Wireless scan result collection, including AP count and AP information array pointer.

### 3.4 `netif_info_t`
Current state information of network interface, including name, hostname, type, state, MAC address, IP configuration, firmware version, wireless configuration, etc.

### 3.5 `netif_config_t`
Network interface configuration parameters, including hostname, wireless configuration, custom MAC address, IP configuration, etc.

---

## 4. Macro Definition Descriptions

- **IP Address Formatting/Checking**: `NETIF_IPV4_STR_FMT`, `NETIF_IPV4_PARAMETER(ip)`, `NETIF_IPV4_IS_ZERO(ip)`
- **MAC Address Formatting/Checking**: `NETIF_MAC_STR_FMT`, `NETIF_MAC_PARAMETER(mac)`, `NETIF_MAC_IS_BROADCAST(mac)`, etc.
- **Network Interface Name Formatting**: `NETIF_NAME_STR_FMT`, `NETIF_NAME_PARAMETER(netif)`
- **String Length Limits**: `NETIF_HOST_NAME_SIZE`, `NETIF_SSID_VALUE_SIZE`, `NETIF_PW_VALUE_SIZE`, etc.

---

## 5. Interface Function Descriptions

### 5.1 Network Interface Control Interface

#### `int netif_manager_ctrl(const char *if_name, netif_cmd_t cmd, void *param);`
- **Function**: Perform control operations on the specified network interface (configure, initialize, enable, get information, state, disable, destroy).
- **Parameters**:
  - `if_name`: Network interface name string
  - `cmd`: Control command (see `netif_cmd_t`)
  - `param`: Command parameter pointer (pass different structures according to command type)
- **Return Value**:
  - Returns 0 on success, negative error code on failure

---

### 5.2 Network Interface Manager Registration

#### `void netif_manager_register(void);`
- **Function**: Register the network interface manager to the system and initialize related resources.
- **Parameters**: None
- **Return Value**: None

---

### 5.3 Get Network Interface Information List

#### `int nm_get_netif_list(netif_info_t **netif_info_list);`
- **Function**: Get information list of all network interfaces
- **Parameters**:
  - `netif_info_list`: Pointer to information list pointer, need to call `nm_free_netif_list` to release
- **Return Value**:
  - Returns number of network interfaces on success, negative number on failure

#### `void nm_free_netif_list(netif_info_t *netif_info_list);`
- **Function**: Release the network interface information list obtained through `nm_get_netif_list`
- **Parameters**:
  - `netif_info_list`: Information list pointer
- **Return Value**: None

---

### 5.4 Get Network Interface State

#### `netif_state_t nm_get_netif_state(const char *if_name);`
- **Function**: Get the state of the specified network interface
- **Parameters**:
  - `if_name`: Network interface name
- **Return Value**:
  - Network interface state (see `netif_state_t`)

---

### 5.5 Get/Set Network Interface Information and Configuration

#### `int nm_get_netif_info(const char *if_name, netif_info_t *netif_info);`
- **Function**: Get detailed information of the specified network interface
- **Parameters**:
  - `if_name`: Network interface name
  - `netif_info`: Information structure pointer
- **Return Value**:
  - Returns 0 on success, negative number on failure

#### `void nm_print_netif_info(const char *if_name, netif_info_t *netif_info);`
- **Function**: Print network interface information
- **Parameters**:
  - `if_name`: Network interface name (preferred)
  - `netif_info`: Information structure pointer
- **Return Value**: None

#### `int nm_get_netif_cfg(const char *if_name, netif_config_t *netif_cfg);`
- **Function**: Get network interface configuration parameters
- **Parameters**:
  - `if_name`: Network interface name
  - `netif_cfg`: Configuration structure pointer
- **Return Value**:
  - Returns 0 on success, negative number on failure

#### `int nm_set_netif_cfg(const char *if_name, netif_config_t *netif_cfg);`
- **Function**: Set network interface configuration parameters
- **Parameters**:
  - `if_name`: Network interface name
  - `netif_cfg`: Configuration structure pointer
- **Return Value**:
  - Returns 0 on success, negative number on failure

---

### 5.6 Network Interface Lifecycle Management

#### `int nm_ctrl_netif_init(const char *if_name);`
- **Function**: Initialize the specified network interface
- **Parameters**:
  - `if_name`: Network interface name
- **Return Value**:
  - Returns 0 on success, negative number on failure

#### `int nm_ctrl_netif_up(const char *if_name);`
- **Function**: Enable the specified network interface
- **Parameters**:
  - `if_name`: Network interface name
- **Return Value**:
  - Returns 0 on success, negative number on failure

#### `int nm_ctrl_netif_down(const char *if_name);`
- **Function**: Disable the specified network interface
- **Parameters**:
  - `if_name`: Network interface name
- **Return Value**:
  - Returns 0 on success, negative number on failure

#### `int nm_ctrl_netif_deinit(const char *if_name);`
- **Function**: Release resources of the specified network interface
- **Parameters**:
  - `if_name`: Network interface name
- **Return Value**:
  - Returns 0 on success, negative number on failure

---

### 5.7 Wireless Scanning Related Interfaces

#### `int nm_wireless_start_scan(wireless_scan_callback_t callback);`
- **Function**: Start wireless scanning, results returned through callback
- **Parameters**:
  - `callback`: Scan result callback function
- **Return Value**:
  - Returns 0 on success, negative number on failure

#### `void nm_print_wireless_scan_result(wireless_scan_result_t *scan_result);`
- **Function**: Print wireless scan results
- **Parameters**:
  - `scan_result`: Scan result structure pointer
- **Return Value**: None

---

## 6. Usage Recommendations

- For all structures with string members, it is recommended to use fixed-length arrays to avoid memory leaks and out-of-bounds access.
- Lists obtained need to be released in time to prevent memory leaks.
- In multi-threaded environments, it is recommended to add lock protection in interface implementation.
- Error codes are recommended to be uniformly defined for easy maintenance and exception handling.
- If IPv6 or multiple network interfaces need to be supported, it is recommended to extend related structures and interfaces.

---

## 7. Example Code Snippet

```c
netif_info_t *info_list = NULL;
int num = nm_get_netif_list(&info_list);
if (num > 0) {
    for (int i = 0; i < num; ++i) {
        nm_print_netif_info(NULL, &info_list[i]);
    }
}
nm_free_netif_list(info_list);
```

---

## 8. Version and Maintenance

- Document Version: V1.0
- Applicable Header File: `netif_manager.h`
- Maintainer: wicevi
- Last Updated: 2025-09-10

---
