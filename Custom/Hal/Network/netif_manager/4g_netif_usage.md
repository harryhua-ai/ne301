# 4G网卡使用说明

## 概述

本文档介绍如何使用4G网卡模块（基于EG912U模组），包括初始化、获取信息/配置、连接、断开以及发送AT指令等功能。

## 重要提示

⚠️ **状态要求**：
- **获取信息/配置**：必须在网卡处于 **DOWN** 状态时才能获取到最新的信息和配置
- **发送AT指令**：必须在网卡处于 **断开（DOWN）** 状态时才能使用，且modem必须处于 **INIT** 状态

## 1. 初始化

### 1.1 API方式

```c
#include "netif_manager.h"

int ret = nm_ctrl_netif_init("4g");
if (ret != 0) {
    // 初始化失败
    printf("4G网卡初始化失败: %d\n", ret);
}
```

### 1.2 命令行方式

```bash
ifconfig 4g init
```

**说明**：
- 初始化会检测并设置UART波特率（支持115200、230400、460800、921600）
- 初始化会获取网卡的基本信息（型号、IMEI、固件版本等）
- 初始化成功后，网卡状态为 **DOWN**

## 2. 获取信息/配置

### 2.1 获取网卡信息

**⚠️ 重要：必须在网卡为DOWN状态时才能获取到最新信息**

#### API方式

```c
#include "netif_manager.h"

// 首先确保网卡处于DOWN状态
netif_state_t state = nm_get_netif_state("4g");
if (state != NETIF_STATE_DOWN) {
    // 如果网卡处于UP状态，需要先断开
    nm_ctrl_netif_down("4g");
}

// 获取网卡信息
netif_info_t netif_info = {0};
int ret = nm_get_netif_info("4g", &netif_info);
if (ret == 0) {
    // 打印信息
    nm_print_netif_info("4g", NULL);
    
    // 或者访问具体字段
    printf("型号: %s\n", netif_info.cellular_info.model_name);
    printf("IMEI: %s\n", netif_info.cellular_info.imei);
    printf("IMSI: %s\n", netif_info.cellular_info.imsi);
    printf("ICCID: %s\n", netif_info.cellular_info.iccid);
    printf("SIM状态: %s\n", netif_info.cellular_info.sim_status);
    printf("运营商: %s\n", netif_info.cellular_info.operator);
    printf("信号强度: %d dBm\n", netif_info.cellular_info.rssi);
    printf("CSQ值: %d, BER: %d\n", netif_info.cellular_info.csq_value, 
           netif_info.cellular_info.ber_value);
    printf("信号等级: %d\n", netif_info.cellular_info.csq_level);
    printf("固件版本: %s\n", netif_info.fw_version);
    
    // 配置信息也包含在netif_info中
    printf("APN: %s\n", netif_info.cellular_cfg.apn);
    printf("用户名: %s\n", netif_info.cellular_cfg.user);
    printf("密码: %s\n", netif_info.cellular_cfg.passwd);
    printf("认证方式: %d\n", netif_info.cellular_cfg.authentication);
    printf("漫游: %s\n", netif_info.cellular_cfg.is_enable_roam ? "启用" : "禁用");
}
```

#### 命令行方式

```bash
# 确保网卡处于DOWN状态
ifconfig 4g down

# 获取并打印信息
ifconfig 4g info
```

**信息字段说明**：
- `model_name`: 设备型号名称
- `imei`: 设备IMEI号
- `imsi`: SIM卡IMSI号
- `iccid`: SIM卡ICCID号
- `sim_status`: SIM卡状态（READY/No SIM Card等）
- `operator`: 当前网络运营商名称
- `rssi`: 接收信号强度指示（dBm）
- `csq_value`: 信号强度值（0~31，99表示无信号）
- `ber_value`: 误码率值
- `csq_level`: 信号强度等级（0~5）
- `version`: 固件版本

### 2.2 获取网卡配置

**⚠️ 重要：必须在网卡为DOWN状态时才能获取到最新配置**

**说明**：通常可以直接从 `netif_info_t` 结构体的 `cellular_cfg` 字段获取配置信息，无需单独调用 `nm_get_netif_cfg()`。只有在需要单独获取配置（不获取其他信息）的情况下，才使用 `nm_get_netif_cfg()`。

#### 方式一：从获取信息中获取配置（推荐）

```c
#include "netif_manager.h"

// 确保网卡处于DOWN状态
netif_state_t state = nm_get_netif_state("4g");
if (state != NETIF_STATE_DOWN) {
    nm_ctrl_netif_down("4g");
}

// 获取信息（包含配置信息）
netif_info_t netif_info = {0};
int ret = nm_get_netif_info("4g", &netif_info);
if (ret == 0) {
    // 从netif_info中获取配置
    printf("APN: %s\n", netif_info.cellular_cfg.apn);
    printf("用户名: %s\n", netif_info.cellular_cfg.user);
    printf("密码: %s\n", netif_info.cellular_cfg.passwd);
    printf("认证方式: %d\n", netif_info.cellular_cfg.authentication);
    printf("漫游: %s\n", netif_info.cellular_cfg.is_enable_roam ? "启用" : "禁用");
    printf("PIN码: %s\n", netif_info.cellular_cfg.pin);
    printf("PUK码: %s\n", netif_info.cellular_cfg.puk);
}
```

#### 方式二：单独获取配置（仅在需要时使用）

```c
#include "netif_manager.h"

// 确保网卡处于DOWN状态
netif_state_t state = nm_get_netif_state("4g");
if (state != NETIF_STATE_DOWN) {
    nm_ctrl_netif_down("4g");
}

// 单独获取配置（仅在需要单独获取配置时使用）
netif_config_t netif_cfg = {0};
int ret = nm_get_netif_cfg("4g", &netif_cfg);
if (ret == 0) {
    printf("APN: %s\n", netif_cfg.cellular_cfg.apn);
    printf("用户名: %s\n", netif_cfg.cellular_cfg.user);
    printf("密码: %s\n", netif_cfg.cellular_cfg.passwd);
    printf("认证方式: %d\n", netif_cfg.cellular_cfg.authentication);
    printf("漫游: %s\n", netif_cfg.cellular_cfg.is_enable_roam ? "启用" : "禁用");
    printf("PIN码: %s\n", netif_cfg.cellular_cfg.pin);
    printf("PUK码: %s\n", netif_cfg.cellular_cfg.puk);
}
```

#### 命令行方式

```bash
# 确保网卡处于DOWN状态
ifconfig 4g down

# 获取配置（通过info命令可以看到配置信息）
ifconfig 4g info
```

**配置字段说明**：
- `apn`: APN（接入点名称）
- `user`: APN用户名
- `passwd`: APN密码
- `authentication`: APN认证方式（0=None, 1=PAP, 2=CHAP等）
- `is_enable_roam`: 是否启用漫游（0=禁用, 1=启用）
- `pin`: SIM卡PIN码
- `puk`: SIM卡PUK码

## 3. 设置配置

### 3.1 API方式

```c
#include "netif_manager.h"

// 确保网卡处于DOWN状态
netif_state_t state = nm_get_netif_state("4g");
if (state != NETIF_STATE_DOWN) {
    nm_ctrl_netif_down("4g");
}

// 方式一：从获取信息中获取配置（推荐）
netif_info_t netif_info = {0};
nm_get_netif_info("4g", &netif_info);

// 将配置复制到netif_config_t结构体
netif_config_t netif_cfg = {0};
memcpy(&netif_cfg.cellular_cfg, &netif_info.cellular_cfg, sizeof(netif_cfg.cellular_cfg));

// 修改配置
strncpy(netif_cfg.cellular_cfg.apn, "cmnet", sizeof(netif_cfg.cellular_cfg.apn) - 1);
strncpy(netif_cfg.cellular_cfg.user, "user", sizeof(netif_cfg.cellular_cfg.user) - 1);
strncpy(netif_cfg.cellular_cfg.passwd, "pass", sizeof(netif_cfg.cellular_cfg.passwd) - 1);
netif_cfg.cellular_cfg.authentication = 1;  // PAP认证
netif_cfg.cellular_cfg.is_enable_roam = 0; // 禁用漫游

// 设置配置
int ret = nm_set_netif_cfg("4g", &netif_cfg);
if (ret != 0) {
    printf("设置配置失败: %d\n", ret);
}
```

**或者使用单独获取配置的方式**：

```c
// 方式二：单独获取配置（仅在需要时使用）
netif_config_t netif_cfg = {0};
nm_get_netif_cfg("4g", &netif_cfg);

// 修改配置
strncpy(netif_cfg.cellular_cfg.apn, "cmnet", sizeof(netif_cfg.cellular_cfg.apn) - 1);
// ... 其他配置修改

// 设置配置
nm_set_netif_cfg("4g", &netif_cfg);
```

### 3.2 命令行方式

```bash
# 确保网卡处于DOWN状态
ifconfig 4g down

# 设置APN（仅支持设置APN）
ifconfig 4g cfg cmnet
```

## 4. 连接（启动网卡）

### 4.1 API方式

```c
#include "netif_manager.h"

// 确保网卡已初始化且处于DOWN状态
netif_state_t state = nm_get_netif_state("4g");
if (state == NETIF_STATE_DEINIT) {
    // 先初始化
    nm_ctrl_netif_init("4g");
}

// 连接网卡
int ret = nm_ctrl_netif_up("4g");
if (ret == 0) {
    printf("4G网卡连接成功\n");
    
    // 获取连接后的IP信息
    netif_info_t netif_info = {0};
    nm_get_netif_info("4g", &netif_info);
    printf("IP地址: %d.%d.%d.%d\n", 
           netif_info.ip_addr[0], netif_info.ip_addr[1],
           netif_info.ip_addr[2], netif_info.ip_addr[3]);
    printf("网关: %d.%d.%d.%d\n",
           netif_info.gw[0], netif_info.gw[1],
           netif_info.gw[2], netif_info.gw[3]);
} else {
    printf("4G网卡连接失败: %d\n", ret);
}
```

**连接流程说明**：
1. 等待SIM卡就绪（最多等待10秒）
2. 设置APN等配置参数
3. 更新网卡信息
4. 进入PPP模式
5. 建立PPP连接
6. 获取IP地址（通过PPP协商）

### 4.2 命令行方式

```bash
ifconfig 4g up
```

## 5. 断开（停止网卡）

### 5.1 API方式

```c
#include "netif_manager.h"

int ret = nm_ctrl_netif_down("4g");
if (ret == 0) {
    printf("4G网卡已断开\n");
}
```

**断开流程说明**：
1. 关闭PPP连接
2. 退出PPP模式
3. 网卡状态变为DOWN

### 5.2 命令行方式

```bash
ifconfig 4g down
```

## 6. 发送AT指令

### 6.1 使用modem命令行工具

**⚠️ 重要：必须在网卡断开（DOWN）状态时才能使用AT指令，且modem必须处于INIT状态**

#### 基本用法

```bash
# 确保网卡处于DOWN状态
ifconfig 4g down

# 发送AT指令（基本格式）
modem AT<命令>

# 示例：查询信号强度
modem AT+CSQ

# 示例：查询SIM卡状态
modem AT+CPIN?

# 示例：查询IMEI
modem AT+GSN

# 示例：查询APN配置
modem AT+CGDCONT?
```

#### 高级用法（带参数）

```bash
# 设置超时时间（-t参数，单位：毫秒）
modem AT+CSQ -t 1000

# 设置期望的响应行数（-r参数）
modem AT+CGDCONT? -r 2

# 组合使用
modem AT+CSQ -t 2000 -r 1
```

**参数说明**：
- `-t <timeout>`: 设置命令超时时间（毫秒），默认500ms
- `-r <num>`: 设置期望的响应行数，默认1行

#### 常用AT指令示例

```bash
# 查询信号强度
modem AT+CSQ

# 查询SIM卡状态
modem AT+CPIN?

# 查询IMEI
modem AT+GSN

# 查询IMSI
modem AT+CIMI

# 查询ICCID
modem AT+QCCID

# 查询运营商信息
modem AT+COPS?

# 查询APN配置
modem AT+CGDCONT?

# 查询APN认证配置
modem AT+QICSGP=1

# 设置APN
modem AT+CGDCONT=1,"IP","cmnet"

# 设置APN认证
modem AT+QICSGP=1,1,"cmnet","user","pass",1

# 查询固件版本
modem AT+CGMR

# 查询设备型号
modem AT+CGMM

# 测试AT通信
modem AT
```

### 6.2 API方式（参考实现）

如果需要通过API方式发送AT指令，可以参考 `ms_modem.c` 中 `modem_cmd_deal_cmd` 函数的实现：

```c
#include "ms_modem.h"
#include "ms_modem_at.h"

// 确保modem处于INIT状态
modem_state_t modem_state = modem_device_get_state();
if (modem_state != MODEM_STATE_INIT) {
    printf("modem is not in init state!\n");
    return -1;
}

// 准备AT命令和响应缓冲区
char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
int rsp_num = 1;  // 期望的响应行数
uint32_t timeout = 500;  // 超时时间（毫秒）

// 分配响应缓冲区
for (int i = 0; i < rsp_num; i++) {
    rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
    if (rsp_list[i] == NULL) {
        printf("malloc rsp_list[%d] failed!\n", i);
        return -1;
    }
}

// 构造AT命令（需要添加\r\n）
snprintf(at_cmd, sizeof(at_cmd), "AT+CSQ\r\n");

// 发送AT命令并等待响应
extern modem_at_handle_t modem_at_handle;
int ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);

if (ret >= MODEM_OK) {
    // 打印响应
    for (int i = 0; i < ret; i++) {
        printf("rsp[%d] = %s\n", i, rsp_list[i]);
    }
} else {
    printf("modem at failed(ret = %d)!\n", ret);
}

// 释放响应缓冲区
for (int i = 0; i < rsp_num; i++) {
    if (rsp_list[i] != NULL) {
        vPortFree(rsp_list[i]);
        rsp_list[i] = NULL;
    }
}
```

## 7. 完整使用示例

### 7.1 初始化并连接

```c
#include "netif_manager.h"

// 1. 初始化
int ret = nm_ctrl_netif_init("4g");
if (ret != 0) {
    printf("初始化失败: %d\n", ret);
    return;
}

// 2. 获取信息（在DOWN状态，包含配置信息）
netif_info_t info = {0};
ret = nm_get_netif_info("4g", &info);
if (ret == 0) {
    printf("IMEI: %s\n", info.cellular_info.imei);
    printf("信号强度: %d dBm\n", info.cellular_info.rssi);
    printf("当前APN: %s\n", info.cellular_cfg.apn);
}

// 3. 设置配置（如果需要修改）
if (strcmp(info.cellular_cfg.apn, "cmnet") != 0) {
    // 从info中获取配置，修改后设置
    netif_config_t cfg = {0};
    memcpy(&cfg.cellular_cfg, &info.cellular_cfg, sizeof(cfg.cellular_cfg));
    strncpy(cfg.cellular_cfg.apn, "cmnet", sizeof(cfg.cellular_cfg.apn) - 1);
    
    ret = nm_set_netif_cfg("4g", &cfg);
    if (ret != 0) {
        printf("设置配置失败: %d\n", ret);
        return;
    }
}

// 4. 连接
ret = nm_ctrl_netif_up("4g");
if (ret == 0) {
    printf("4G网卡连接成功\n");
    
    // 获取连接后的IP信息
    ret = nm_get_netif_info("4g", &info);
    if (ret == 0) {
        printf("IP地址: %d.%d.%d.%d\n", 
               info.ip_addr[0], info.ip_addr[1],
               info.ip_addr[2], info.ip_addr[3]);
        printf("网关: %d.%d.%d.%d\n",
               info.gw[0], info.gw[1],
               info.gw[2], info.gw[3]);
        printf("子网掩码: %d.%d.%d.%d\n",
               info.netmask[0], info.netmask[1],
               info.netmask[2], info.netmask[3]);
    }
} else {
    printf("4G网卡连接失败: %d\n", ret);
}
```

### 7.2 断开并使用AT指令

```c
#include "netif_manager.h"
#include "ms_modem.h"
#include "ms_modem_at.h"

// 1. 断开网卡
int ret = nm_ctrl_netif_down("4g");
if (ret != 0) {
    printf("断开网卡失败: %d\n", ret);
    return;
}

// 2. 确保modem处于INIT状态
modem_state_t state = modem_device_get_state();
if (state != MODEM_STATE_INIT) {
    printf("modem状态错误: %d，当前状态: %d\n", state, state);
    return;
}

// 3. 发送AT指令（使用API方式）
extern modem_at_handle_t modem_at_handle;
char at_cmd[MODEM_AT_CMD_LEN_MAXIMUM] = {0};
char *rsp_list[MODEM_AT_RSP_MAX_LINE_NUM] = {0};
int rsp_num = 1;
uint32_t timeout = 500;

// 分配响应缓冲区
for (int i = 0; i < rsp_num; i++) {
    rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
    if (rsp_list[i] == NULL) {
        printf("分配响应缓冲区失败\n");
        return;
    }
}

// 发送AT+CSQ命令查询信号强度
snprintf(at_cmd, sizeof(at_cmd), "AT+CSQ\r\n");
ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);
if (ret >= MODEM_OK) {
    printf("信号强度响应: %s\n", rsp_list[0]);
} else {
    printf("AT命令执行失败: %d\n", ret);
}

// 发送AT+CPIN?命令查询SIM卡状态
snprintf(at_cmd, sizeof(at_cmd), "AT+CPIN?\r\n");
rsp_num = 2;  // 需要2行响应
for (int i = 1; i < rsp_num; i++) {
    if (rsp_list[i] == NULL) {
        rsp_list[i] = (char *)pvPortMalloc(MODEM_AT_RSP_LEN_MAXIMUM);
    }
}
ret = modem_at_cmd_wait_rsp(&modem_at_handle, at_cmd, rsp_list, rsp_num, timeout);
if (ret >= MODEM_OK) {
    for (int i = 0; i < ret; i++) {
        printf("SIM状态响应[%d]: %s\n", i, rsp_list[i]);
    }
} else {
    printf("AT命令执行失败: %d\n", ret);
}

// 释放响应缓冲区
for (int i = 0; i < MODEM_AT_RSP_MAX_LINE_NUM; i++) {
    if (rsp_list[i] != NULL) {
        vPortFree(rsp_list[i]);
        rsp_list[i] = NULL;
    }
}
```

### 7.3 命令行完整流程

```bash
# 1. 初始化
ifconfig 4g init

# 2. 查看信息（自动处于DOWN状态）
ifconfig 4g info

# 3. 设置APN（可选）
ifconfig 4g cfg cmnet

# 4. 连接
ifconfig 4g up

# 5. 查看连接后的信息
ifconfig 4g info

# 6. 断开
ifconfig 4g down

# 7. 发送AT指令
modem AT+CSQ
modem AT+CPIN?
modem AT+GSN

# 8. 重新连接
ifconfig 4g up
```

## 8. 状态说明

### 8.1 网卡状态

- `NETIF_STATE_DEINIT`: 未初始化
- `NETIF_STATE_DOWN`: 已初始化但未连接（可以获取信息/配置，可以发送AT指令）
- `NETIF_STATE_UP`: 已连接（可以正常使用网络）

### 8.2 Modem状态

- `MODEM_STATE_UNINIT`: 未初始化
- `MODEM_STATE_INIT`: 已初始化（可以发送AT指令）
- `MODEM_STATE_PPP`: PPP模式（已连接）

## 9. 错误处理

### 9.1 常见错误码

- `AICAM_OK (0)`: 成功
- `AICAM_ERROR_INVALID_PARAM`: 参数错误
- `AICAM_ERROR_BUSY`: 设备忙（通常是因为状态不正确）
- `AICAM_ERROR_TIMEOUT`: 超时
- `AICAM_ERROR_NOT_INITIALIZED`: 未初始化
- `MODEM_ERR_INVALID_STATE`: Modem状态错误
- `MODEM_ERR_TIMEOUT`: Modem操作超时

### 9.2 错误处理示例

```c
int ret = nm_ctrl_netif_up("4g");
if (ret != 0) {
    switch (ret) {
        case AICAM_ERROR_BUSY:
            printf("网卡正在使用中，请先断开\n");
            break;
        case AICAM_ERROR_TIMEOUT:
            printf("连接超时，请检查SIM卡和信号\n");
            break;
        default:
            printf("连接失败: %d\n", ret);
            break;
    }
}
```

## 10. 注意事项

1. **状态检查**：在执行任何操作前，建议先检查网卡状态
2. **获取信息时机**：获取信息/配置时，必须在DOWN状态才能获取最新数据
3. **AT指令限制**：发送AT指令时，网卡必须处于DOWN状态，且modem必须处于INIT状态
4. **配置更新**：修改配置后，需要重新连接才能生效
5. **SIM卡状态**：连接前确保SIM卡已就绪（READY状态）
6. **信号强度**：连接前建议检查信号强度，信号太弱可能导致连接失败
7. **超时设置**：AT指令的超时时间根据命令复杂度适当调整

## 11. 相关API参考

### 11.1 网卡管理API

- `nm_ctrl_netif_init()`: 初始化网卡
- `nm_ctrl_netif_up()`: 启动/连接网卡
- `nm_ctrl_netif_down()`: 停止/断开网卡
- `nm_ctrl_netif_deinit()`: 反初始化网卡
- `nm_get_netif_state()`: 获取网卡状态
- `nm_get_netif_info()`: 获取网卡信息
- `nm_get_netif_cfg()`: 获取网卡配置
- `nm_set_netif_cfg()`: 设置网卡配置
- `nm_print_netif_info()`: 打印网卡信息

### 11.2 Modem API

- `modem_device_init()`: 初始化modem
- `modem_device_deinit()`: 反初始化modem
- `modem_device_get_state()`: 获取modem状态
- `modem_device_get_info()`: 获取modem信息
- `modem_device_get_config()`: 获取modem配置
- `modem_device_set_config()`: 设置modem配置
- `modem_device_into_ppp()`: 进入PPP模式
- `modem_device_exit_ppp()`: 退出PPP模式

### 11.3 AT指令API

- `modem_at_cmd_wait_ok()`: 发送AT命令并等待OK响应
- `modem_at_cmd_wait_str()`: 发送AT命令并等待指定字符串
- `modem_at_cmd_wait_rsp()`: 发送AT命令并等待多行响应

## 12. 故障排查

### 12.1 初始化失败

- 检查硬件连接
- 检查电源供电
- 查看串口配置是否正确

### 12.2 连接失败

- 检查SIM卡是否插入且状态为READY
- 检查信号强度（CSQ值）
- 检查APN配置是否正确
- 检查运营商网络是否可用

### 12.3 AT指令无响应

- 确保网卡处于DOWN状态
- 确保modem处于INIT状态
- 检查AT命令格式是否正确（需要包含\r\n）
- 适当增加超时时间

### 12.4 获取信息不准确

- 确保在DOWN状态时获取信息
- 如果网卡处于UP状态，先断开再获取

---

**文档版本**: 1.0  
**最后更新**: 2024

