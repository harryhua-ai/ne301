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
| AC4 可复核证据 + 环境说明 + 未覆盖项 | 本文档（§7 含命令/原始输出/完整 SHA256 与备份↔恢复映射）；真机 AC 已通过。恢复状态如实分述：App1/OTA 字节级还原，LittleFS 持有 2 个残留测试文件未还原（§7.5） |

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

构建命令：基线 `make app`；实验配置 `make app APP_HOST_POC=1`；默认配置（改动后）`make app`；打包 `make pkg-app APP_HOST_POC=1`。

| 配置 | 结果 | 关键数据（链接器原样输出） |
|---|---|---|
| 基线（无改动） | PASS | `AXISRAM1_2_S: 3841588 B 4095 KB 91.61%`、`SRAM_POOL: 902400 B 1736 KB 50.76%`、`AXI_SRAM_UNCACHED: 189728 B 312 KB 59.39%`、`PSRAM: 57273376 B 60 MB 91.03%`；`text 3431424 / data 410132 / bss 58365496` |
| `APP_HOST_POC=1` | PASS | 同上但 `AXISRAM1_2_S: 3844340 B`（+2752）、text `3434184`（+2760）、新增一行 `APP_HOST_RAM: 0 B 2 MB 0.00%`；ASSERT 通过 |
| 默认（改动后，flag=0） | PASS | 与基线**逐位一致**（text/data/bss 相同，二进制无 apphost 痕迹） |

链接 map 原样摘录（`Appli/build/ne301_App.map`，实验配置）：

```
0x93a9ec20  __psram_bss_end__ = .
0x93e00000  __app_host_ram_base__ = ORIGIN (APP_HOST_RAM)
0x94000000  __app_host_ram_end__ = (ORIGIN (APP_HOST_RAM) + LENGTH (APP_HOST_RAM))
ASSERT ((__psram_bss_end__ <= ORIGIN (APP_HOST_RAM)), APP_HOST_RAM overlaps .psram_bss)
0x901caacc  app_host_load_and_run
0x901cae0c  app_host_validate
```

## 6. 静态验证

- `tests/app_host && make test` → 原样输出 `app_host_validate: 21 checks, 0 failures`：合法镜像 OK、文件截断、坏 magic、头长不足、未来格式、ABI 不匹配、目标地址错误、镜像 0 长度/超区/超文件、入口越界/未对齐、CRC 损坏、NULL 参数——AC3 全部失败路径在同一实现上被构造并验证（ASan 重建同样 21/21 无告警）。
- 测试镜像 `tests/app_host/testapp`：固定链接 0x93E00000 的裸机 thumb 镜像，打包器原样输出 `packed apphost_test.bin: payload 120 bytes, entry_offset 0, crc 0x7a13dd20`（成品 152B，md5 `4224e5167ada01fabf0bcbfbae31ab7a`），消费 `app_host_abi.h`，经函数表调用 `log`/`tick_ms` 后返回 `0x600D`。

## 7. 真机验证（已通过；恢复状态如实分述）

### 7.0 证据身份声明

- 真机运行的**代码**为 `90f641f8`（`feat:` 提交，设备 `version` 原样输出 `Git Hash: 90f641f8 / Branch: agent/25/6857bbfa` 可证）。
- 其后的提交（首次验证后的文档修订 `709e2546`、本次修正及后续同类修订）均**仅修改本文件**，未触碰代码；`90f641f8` 上的工程/真机证据持续复用有效。当前 exact Candidate 身份以 canonical `ght` 交付记录与 PR Current Candidate Snapshot 为准，不由本文档自述。
- 平台/工具：STM32N6570-DK Rev B，ST-Link V2J46S7，STM32CubeProgrammer v2.19.0（外存访问用 `-el MX66UW1G45G_STM32N6570-DK.stldr`）；读写均要求拨码 SW2 处于烧录位（正常位 SWD/UR 均不可达，实测）。

### 7.1 验证对象与命令（地址/长度/版本）

统一命令形：`STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el <EL> -u <addr> <len> <file>`（读）、`... -w <file> <addr> --verify`（写，CubeProgrammer 自动擦除受影响扇区）。除下表标注外，App2/FSBL/NVS/WEB/AI 分区只读或未触碰。

| 分区 | 地址 | 长度 | 动作 |
|---|---|---|---|
| FSBL | 0x70000000 | 0x80000 (512KB) | 只读备份 |
| OTA state | 0x70090000 | 0x2000 (8KB) | 只读备份 + 恢复后复测 |
| App1 | 0x70100000 | 0x400000 (4MB) | 备份 → 刷 PoC → 恢复 → 复测 |
| App2 | 0x70500000 | 0x400000 (4MB) | 只读备份（原厂 4.3.1.276 回退槽） |
| LittleFS | 0x71D00000 | 0x6000000 (96MB, 6×16MB 分块, 0 错误) | 备份 → FS 层注入 2 文件 → 回写 2 块 |
| NVS | 0x70080000 | 64KB | **未读未写**（内容未知，非本任务触碰对象） |
| WEB / AI | 0x71900000+ | — | **未读未写** |

烧录镜像版本：`build/ne301_App_signed_v4.3.1.292_pkg.bin`（OTA 头 `OTAU`，payload 3,845,376 B），由 `make pkg-app APP_HOST_POC=1` 从 `90f641f8` 构建签名。

### 7.2 备份 ↔ 恢复映射（完整 SHA256）

| 对象 | 烧录前备份 SHA256 | 恢复/复测后 SHA256 | 结论 |
|---|---|---|---|
| App1 全槽 | `ddaca651dcb0789fbb15c4f84f98cf0b60d98e1c846dc4970ed6c4b9504d8b12` | `ddaca651dcb0789fbb15c4f84f98cf0b60d98e1c846dc4970ed6c4b9504d8b12`（`-w` 备份回写 `--verify` 后 `-u` 全量回读） | **字节级还原** |
| OTA state | `9b9b633d9416084a53cf47c8c7b9717606d4a6e70fd220b32e145ec0dfc407ce` | 同左（复测读取） | **未变**（全程未写） |
| FSBL | `17918a14b38730a1270278fef2da29669f4307c8efdd45b97f31c7b4cb752712` | —（只读） | 未触碰 |
| App2 | `feb2f6e460952aa57274daa91ad51878e0ed6670c3ad56123ab52511e335f686` | —（只读） | 未触碰 |
| LittleFS 全分区 | `2edb4e5d2d2aec3fbf6bcbec879c2d4cef979598f779b4ee085f3dd655c8acff` | **未整体还原**，见 §7.5 | 持有残留差异 |

写操作原始输出摘录（各一次）：`Download verified successfully`（PoC 刷写与 App1 恢复两次 `-w ... --verify` 均为该结果）。

### 7.3 LittleFS 注入（测试镜像入设备的替代通道）

固件 `debug_init_ymodem` 为未实现桩（ymodem 接收不可用）且设备不在可达局域网，故采用文件系统层注入：以**固件自带 littlefs v2.1 源码**（`Custom/Common/Lib/littlefs`，`LFS_DISK_VERSION 0x00020001`）编译 host 工具（源码已随本 Candidate 提交于 `tests/app_host/lfstool/`，构建命令：

```
cc -I tests/app_host/lfstool/shim -I Custom/Common/Lib/littlefs \
   -o lfs_tool tests/app_host/lfstool/lfs_tool.c \
   Custom/Common/Lib/littlefs/lfs.c Custom/Common/Lib/littlefs/lfs_util.c \
   tests/app_host/lfstool/host_impl.c -std=c11 -O1
```

），对 96MB 分区镜像执行正规 FS 操作：

- 首次以 8MB 前缀挂载失败（元数据对已迁移至 `{0x34b5, 0x34b6}` ≈ 52.7MB）→ 全量转储后挂载成功（`mounted, block_count=24576`）。
- 注入 `/apps/apphost_test.bin`（152B）与 `/apps/apphost_corrupt.bin`（152B，CRC 翻转变体），工具原样输出两条 `added ... -> ... (152 bytes)`；全卷仅 **2 个 4K 块变更**（块 1、块 21685）。
- 变更块回写地址 `0x71d01000`、`0x771b5000`（`--verify`），回读 SHA256 与写入文件一致：`6a38631c56648bfd9736a38126bf591324ae00ca6dc8a135158cced206982dbf`、`f126b6d4467f2fcec847069f6fc1b6758d65581bd7a89df93e413cda0221173f`。

**CRC 通路说明（更正此前"标准 IEEE"的错误表述）**：

- host 工具的 `CRC_Accumulate`（`tests/app_host/lfstool/host_impl.c`，源码可查）为逐字节、init/final XOR `0xFFFFFFFF`、**反射多项式 `0x82F63B78` 的实现——即 CRC-32C/Castagnoli 的反射形式**；CRC-32/IEEE 的反射多项式为 `0xEDB88320`，两者不同，勿混同。
- 固件自身 littlefs CRC 路径：`lfs_util.h` 将 `LFS_CRC32` 指到 `CRC_Accumulate`（`Appli/Core/Inc/crc.h`），实现走 **CRC 硬件外设**（`Appli/Core/Src/crc.c`：`MX_CRC_Init` poly `0x04C11DB7`、32 位、INIT `0xFFFFFFFF`、输入/输出均不反转；`CRC_Accumulate` → `HAL_CRC_Accumulate`）。**shim 多项式与该硬件配置的确切等价性未在本验证中证明（未知）**；仅可陈述行为事实：shim 成功挂载固件创建的卷，且设备重启后接受 shim 写入的元数据并读出所注入文件（§7.4），即在被操作的卷上行为等价。
- 测试镜像 payload 的完整性校验是**独立通路**：`generic_crc32`（`Custom/Common/Utils/generic_math.c`，CRC-32/IEEE 软件表实现，与 zlib 一致），与 LittleFS 元数据 CRC 无关。

### 7.4 AC2 / AC3 真机结果（控制台原样摘录）

设备 `version`：`Firmware Version: 4.3.1.292 / Git Hash: 90f641f8 / Branch: agent/25/6857bbfa`。

```
apphost info
apphost: exec region 0x93e00000-0x94000000 (2097152 bytes)
apphost: abi 0x0001.0000 default image /apps/apphost_test.bin
apphost: littlefs mounted

apphost load /apps/apphost_test.bin
apphost: run /apps/apphost_test.bin entry=0x93e00000 size=120 abi=0x0001.0000
[APP] ne301 apphost test app: abi v1 table ok
apphost: entry returned 24589

apphost load /apps/apphost_corrupt.bin
apphost: reject /apps/apphost_corrupt.bin (crc mismatch)
```

- AC2：外部代码经版本化函数表调用 Host `log`/`tick_ms` 并返回 `0x600D`（=24589），cache 维护后执行正确。
- AC3：损坏镜像被拒绝且平台后续抓拍周期照常完成；CLI `reset` 重启后 45 秒启动日志中 apphost/APP 相关行为 **0 条**（默认关闭、无自启循环）。

### 7.5 恢复状态（如实分述）

- **App1 / OTA state：已字节级还原**（映射见 §7.2）；恢复后设备 `version` = `Firmware Version: 4.3.1.276 / Git Hash: 4707dfc2 / Branch: main`（原厂固件）。
- **LittleFS：未还原**。设备上残留 `/apps/apphost_test.bin` 与 `/apps/apphost_corrupt.bin` 两个 152B 惰性数据文件（备份基线 `2edb4e5d…` 已存档；如决定清除，按 §7.3 同法回写原始 2 块即可，是否执行属安全恢复方案决策，非本纠正必须项）。

## 8. 未覆盖项 / 已知限制

- 仅 64MB PSRAM 板型（DK 默认）；32MB 变体显式 fail-closed。
- 同步单入口：无线程/回调卸载/AI 订阅/HTTP 动态路由/自动启动（Contract non-goals）。
- `.neapp` 安装器、生产签名、通用动态链接、ELF loader 均不在本 PoC。
- `STM32N657L0HXQ_LRUN_32MB.ld` 未加执行区（该配置被构建旋钮禁止）。
- 测试基建缺口（与本 PoC 无关的固件事实）：`debug_init_ymodem` 为未实现桩（UART ISR 在 YMODEM 模式丢弃输入且模式不可恢复，仅重启可退出），二进制文件入设备无现成通道；本验证采用文件系统层注入绕过。
- NVS 分区内容未读取（未知）；本任务从未写入该分区。
