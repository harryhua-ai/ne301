# RTMP 库适配说明

本文档说明如何将 RTMP 库适配到嵌入式系统中，包括内存管理和网络部分。

## 内存适配

### 已完成的适配

1. **内存分配函数替换**
   - `malloc` → `hal_mem_alloc_large`
   - `calloc` → `hal_mem_calloc_large`
   - `free` → `hal_mem_free`
   - `realloc` → `rtmp_realloc_impl` (自定义实现)

2. **适配位置**
   - `librtmp/rtmp_sys.h` - 系统适配层，定义了内存函数宏
   - `rtmp_publisher.c` - 直接使用 `hal_mem_*` 函数

### realloc 实现说明

由于 `hal_mem` 不提供获取已分配内存大小的接口，`realloc` 实现采用以下策略：

1. 如果 `ptr == NULL`，等同于 `hal_mem_alloc_large(size)`
2. 如果 `size == 0`，等同于 `hal_mem_free(ptr)`
3. 否则，分配新内存并复制数据
   - 复制大小限制为 `min(new_size, 128KB)` 以确保安全
   - RTMP 库通常使用 realloc 增长缓冲区，此实现适用于该场景

### 使用注意事项

- 所有内存分配使用外部 PSRAM（`hal_mem_alloc_large`）
- 确保在初始化 RTMP 库之前已初始化 HAL 内存池
- 如果遇到内存不足，检查 PSRAM 配置和可用空间

## 网络适配

### 已完成的适配

1. **LWIP Socket API**
   - 在 `rtmp_sys.h` 中检测嵌入式系统并包含 LWIP 头文件
   - 使用 `lwip/sockets.h` 替代标准 socket API
   - 使用 `lwip/netdb.h` 进行 DNS 解析
   - 使用 `lwip/inet.h` 进行地址转换

2. **时间函数**
   - `RTMP_GetTime()` 使用 `osKernelGetTickCount()` 获取系统时间
   - `msleep()` 使用 `osDelay()` 实现

3. **Socket 函数映射**
   - `closesocket()` → `close()`
   - `GetSockError()` → `errno`
   - `SetSockError()` → `errno = e`

### 网络配置要求

1. **LWIP 配置**
   - 确保 LWIP 已正确初始化
   - 确保网络接口已配置并可用
   - 确保 DNS 解析功能可用

2. **网络栈初始化顺序**
   ```
   1. 初始化 HAL 内存池
   2. 初始化 LWIP 网络栈
   3. 配置网络接口
   4. 创建 RTMP 发布者
   5. 连接到 RTMP 服务器
   ```

## 编译配置

### 必需的定义

在编译时定义以下宏以启用适配：

```c
#define USE_LWIP          // 或
#define __ARM_ARCH        // 或
#define STM32             // 或
#define STM32N6
```

### Makefile 配置示例

```makefile
# RTMP Library
C_SOURCES += Custom/Common/Lib/rtmpdump/rtmp_publisher.c
C_SOURCES += Custom/Common/Lib/rtmpdump/librtmp/rtmp.c
C_SOURCES += Custom/Common/Lib/rtmpdump/librtmp/amf.c
C_SOURCES += Custom/Common/Lib/rtmpdump/librtmp/log.c
C_SOURCES += Custom/Common/Lib/rtmpdump/librtmp/parseurl.c
C_SOURCES += Custom/Common/Lib/rtmpdump/librtmp/hashswf.c

C_INCLUDES += -ICustom/Common/Lib/rtmpdump
C_INCLUDES += -ICustom/Common/Lib/rtmpdump/librtmp

# 定义嵌入式系统宏
CFLAGS += -DUSE_LWIP
CFLAGS += -DSTM32N6
```

## 已知限制

1. **realloc 实现**
   - 无法获取旧内存大小，使用保守的复制策略
   - 对于非常大的缓冲区（>128KB），可能无法完全复制
   - 实际使用中，RTMP 缓冲区通常较小，此限制影响不大

2. **内存碎片**
   - 使用 PSRAM 分配，需要注意内存碎片
   - 建议定期检查内存使用情况

3. **线程安全**
   - 当前实现不是线程安全的
   - 如果需要在多线程环境中使用，需要添加互斥锁

## 测试建议

1. **内存测试**
   - 测试长时间运行的内存泄漏
   - 测试大量帧发送的内存使用
   - 监控 PSRAM 使用情况

2. **网络测试**
   - 测试网络断开重连
   - 测试 DNS 解析失败处理
   - 测试超时处理

3. **性能测试**
   - 测试不同帧率和分辨率的性能
   - 测试内存分配性能
   - 测试网络发送性能

## 故障排查

### 内存问题

- **问题**: 内存分配失败
- **解决**: 检查 PSRAM 配置，增加可用内存，检查内存碎片

### 网络问题

- **问题**: 连接失败
- **解决**: 检查网络配置，确保 LWIP 已初始化，检查 DNS 解析

### 编译问题

- **问题**: 未定义的函数
- **解决**: 确保定义了正确的宏（USE_LWIP, STM32N6 等），确保包含正确的头文件

