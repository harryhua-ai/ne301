# App Host PoC（Issue #25）设计与验证报告

- Issue: harryhua-ai/ne301#25（`ght-contract` 为准）
- 分支: `agent/25/6857bbfa`（base `experiment/app-host-poc` @ `660bb3a9`）
- 定位: 可行性 PoC，非产品 App Manager。默认关闭，仅受控开发设备经 UART CLI 手工触发。

## 1. 结论摘要

| 项 | 状态 |
|---|---|
| AC1 实验配置可构建 + 有边界校验的执行 RAM 区 | 通过（构建证据见 §5） |
| AC2 独立镜像装载 + 版本化函数表执行 | **真机通过**（§7.2：装载 0x93E00000、函数表调用返回 0x600D） |
| AC3 失败路径拒绝 + 重启默认关闭 + 不写保护分区 | **真机通过**（§7.3：CRC 损坏拒绝、重启零自动装载；host 测试 21/21） |
| AC4 可复核证据 + 环境说明 + 未覆盖项 | 本文档；真机 AC 已通过（§7），板卡已恢复原状（§7.4） |

## 2. 架构

```
LittleFS (/apps/apphost_test.bin)
  → 读入 PSRAM 顶部 APP_HOST_RAM (0x93E00000, 2MB)   [staging=exec 区, 零额外 RAM]
  → app_host_validate: magic/头长/格式版本/ABI/目标地址/镜像与入口边界/CRC32
  → memmove 移除头, 尾部清零 → D-cache clean+invalidate → I-cache invalidate → DSB/ISB
  → 经 app_host_api_table_t {table_size, abi_version, log, tick_ms} 调用 entry(api, abi)
  → 捕获返回值 → 清零执行区（残留代码不留驻）
```

- 校验失败：不执行、清零区域、日志给出机器可读原因；平台继续正常运行。
- 无启动路径接线、无 auto_start、无线程：实验 App 执行天然默认关闭，重启后必须再次手工 `apphost load`，不构成坏镜像启动循环。
- 全程只使用 LittleFS 文件 API（`flash_lfs_*`，几何上限定在 LITTLEFS 分区 0x71D00000 起），不触碰 APP1/APP2/FSBL/OTA/WEB/AI 分区；装载路径无任何 Flash 写。

## 3. 文件

| 文件 | 作用 |
|---|---|
| `Custom/Common/Inc/app_host_abi.h` | 唯一公开 ABI 源：镜像头、ABI 版本、函数表类型（App 方读此头，不复制私有头） |
| `Custom/Services/AppHost/app_host_validate.{h,c}` | 纯逻辑校验（无 HAL 依赖），固件与 host 测试共用同一实现 |
| `Custom/Services/AppHost/app_host.{h,c}` | 装载器 + `apphost` CLI（info / load [path]） |
| `Custom/Core/Log/cli_cmd.c` | `#if NE301_APP_HOST_POC` 下注册 `apphost` 命令 |
| `Appli/Makefile` | `APP_HOST_POC ?= 0` 旋钮（条件源文件/宏/include；非 64MB PSRAM 配置 fail-closed） |
| `Appli/STM32N657L0HXQ_LRUN.ld` | `APP_HOST_RAM` region + `__app_host_ram_base__/end__` 符号 + `ASSERT(__psram_bss_end__ <= ORIGIN(APP_HOST_RAM))` |
| `tests/app_host/` | host 端校验测试（`make test`）+ ARM 测试镜像（`make testapp`） |

镜像格式（32 字节头，LE）：`magic 'NEA1' | header_size | format_version | abi_version | target_addr | image_size | entry_offset | reserved | crc32(payload)`。CRC-32（IEEE，同 `generic_crc32`/zlib）仅为**完整性**校验，不是代码来源认证；未经生产信任校验的执行能力只存在于 `APP_HOST_POC=1` 的显式受控开发构建。

## 4. 内存边界（构建期机械证明）

执行区为 PSRAM 物理窗口（0x90000000–0x93FFFFFF）顶端 2MB：

- `__psram_bss_end__ = 0x93A9EC20`（含 52MB slab 池 + 全部 `.psram_bss`）< `0x93E00000`，实际余量 ≈ 3.4MB；
- 链接脚本 `ASSERT` 使任何越界布局直接构建失败；非 64MB PSRAM 配置 Makefile 直接 `$(error)`。
- 主固件 4MB 执行区 0x90000000–0x903FFFFF、NPU AXISRAM、FSBL 区均不受影响；无需 MPU 改动（PSRAM 背景映射已可执行，主固件 `.text` 即在其中）。

## 5. 构建证据（本分支新鲜构建，arm-gcc 15.2.Rel1）

| 配置 | 结果 | 关键数据 |
|---|---|---|
| 基线（无改动） | PASS | text 3431424 / data 410132 / bss 58365496；PSRAM 91.03% |
| `APP_HOST_POC=1` | PASS | text 3434184（+2760）；`APP_HOST_RAM: 0 B / 2MB`；ASSERT 通过 |
| 默认（改动后，flag=0） | PASS | 与基线**逐位一致**（text/data/bss 相同，二进制无 apphost 痕迹） |

## 6. 静态验证

- `tests/app_host && make test` → `21 checks, 0 failures`：合法镜像 OK、文件截断、坏 magic、头长不足、未来格式、ABI 不匹配、目标地址错误、镜像 0 长度/超区/超文件、入口越界/未对齐、CRC 损坏、NULL 参数——AC3 全部失败路径在同一实现上被构造并验证（含 ASan 干净通过）。
- 测试镜像 `tests/app_host/testapp`：固定链接 0x93E00000 的裸机 thumb 镜像（120B payload，entry_offset 0，CRC 0x7a13dd20），消费 `app_host_abi.h`，经函数表调用 `log`/`tick_ms` 后返回 `0x600D`。

## 7. 真机验证（已通过，板卡已恢复原状）

### 7.1 环境与隔离记录

- 板卡：ST-LINK SN `56FF6F064984524928381287`（V2J46S7），STM32N6570 Rev B；烧录/读取需物理拨码 SW2 置烧录位（正常位下 SWD 不可达，实测一致）。
- 烧录前字节级备份（SHA256）：App1 `ddaca651…`、App2 `feb2f6e4…`、FSBL `17918a14…`、OTA-state `9b9b633d…`、LittleFS 全分区 `2edb4e5d…`。
- 刷写 PoC 固件：仅 App1 槽（`ne301_App_signed_v4.3.1.292_pkg.bin`，`--verify` 通过）；App2（原厂 4.3.1.276）、FSBL、OTA-state、NVS、AI、Web 分区全程未写。

### 7.2 AC2 真机结果

- `version` → `Firmware Version: 4.3.1.292 / Git Hash: 90f641f8 / Branch: agent/25/6857bbfa`（即本 Candidate 构建）。
- `apphost info` → `exec region 0x93e00000-0x94000000 (2097152 bytes)`、`abi 0x0001.0000`、`littlefs mounted`（链接符号与 ABI 常量真机一致）。
- 测试镜像经文件系统层注入 LittleFS（`/apps/apphost_test.bin`，152B）；`apphost load /apps/apphost_test.bin`：
  ```
  apphost: run /apps/apphost_test.bin entry=0x93e00000 size=120 abi=0x0001.0000
  [APP] ne301 apphost test app: abi v1 table ok
  apphost: entry returned 24589
  ```
  外部代码经版本化函数表调用 Host `log`/`tick_ms` 并返回 `0x600D`，cache 维护后执行正确。

### 7.3 AC3 真机结果

- `apphost load /apps/apphost_corrupt.bin`（CRC 翻转变体）→ `apphost: reject /apps/apphost_corrupt.bin (crc mismatch)`，平台后续抓拍周期照常完成（拒绝不影响运行）。
- CLI `reset` 重启：45 秒启动日志中 apphost/APP 相关行为 **0 条**（存档 `/tmp` 构建证据）——重启默认关闭、无自动装载确认，随后 `apphost info` 仍正常。

### 7.4 恢复证据

- App1 回写备份后全量回读：SHA256 `ddaca651…` == 烧录前备份（字节级一致，恢复 4.3.1.276 / Git `4707dfc2`）。
- OTA-state 回读：`9b9b633d…` == 初始值（全程未变）。
- 遗留说明：LittleFS 中残留两个 152B 测试文件（`/apps/apphost_test.bin`、`/apps/apphost_corrupt.bin`），为惰性数据；如需彻底清除可再以同样方式回写原分区两块（变更基线已存档）。

## 8. 未覆盖项 / 已知限制

- 仅 64MB PSRAM 板型（DK 默认）；32MB 变体显式 fail-closed。
- 同步单入口：无线程/回调卸载/AI 订阅/HTTP 动态路由/自动启动（Contract non-goals）。
- `.neapp` 安装器、生产签名、通用动态链接、ELF loader 均不在本 PoC。
- `STM32N657L0HXQ_LRUN_32MB.ld` 未加执行区（该配置被构建旋钮禁止）。
- 测试基建缺口（与本 PoC 无关的固件事实）：`debug_init_ymodem` 为未实现桩（UART ISR 在 YMODEM 模式丢弃输入且模式不可恢复，仅重启可退出），二进制文件入设备无现成通道；本验证采用文件系统层注入绕过。
