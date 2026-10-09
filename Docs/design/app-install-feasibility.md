# App 安装事务、掉电恢复与设备验签可行性调查（Issue #27 Spike）

- Issue: harryhua-ai/ne301#27（以 issue 内 `ght-contract` 为准）
- 调查基线: worktree `agent/27/f56e29f0`，HEAD `a5b4bf3d`（= `experiment/app-host-poc` 合入 PR #26 后的实验分支 HEAD，含 Issue #25 全部 PoC 代码与 `Docs/design/app-host-poc.md`）
- 定位: **调查证据文档，不是规范，不是产品决策权威**。纯静态源码分析 + 复用 #25 历史真机证据；本会话无实时设备，未做任何写入/刷写/OTA 操作。
- 证据等级标注:
  - 【源码/静态分析】= 给出 `file:line` 锚点，读者可直接对照基线 commit 核对
  - 【历史真机】= 来自 Issue #25 文档（`Docs/design/app-host-poc.md` §7）或 issue 描述的已留存设备证据，本会话未复测
  - 【推断】= 从源码语义/组件设计文档推导，未经设备证实
  - 【尚需验证】= 当前不可证实，列入 §5 清单

---

## 1. AC1：设备能力与限制清单

### 1.1 分区表与 App 可用存储

【源码/静态分析】`Custom/Common/Inc/mem_map.h:18-121`、根 `Makefile:43-77`（两侧一致）:

| 分区 | 地址 | 大小 | 备注 |
|---|---|---|---|
| FSBL | 0x70000000 | 512K | 无备用槽 |
| NVS | 0x70080000 | 64K | factory 32K + user 32K（`storage.h:37-42`） |
| OTA state | 0x70090000 | 8K | SystemState，CRC32 保护 |
| SWAP | 0x70092000 | 64K | |
| APP1 / APP2 | 0x70100000 / 0x70500000 | 各 4M | **唯一真 A/B 双槽（仅 FIRMWARE_APP）** |
| AI_1 / AI_2 | 0x70900000 / 0x71100000 | 各 8M | 单槽（A==B） |
| WEB | 0x71900000 | 1M | 单槽 |
| WiFi FW | 0x71A00000 | 3M | 单槽 |
| LittleFS | 0x71D00000 | 96M(128MB 闪存) / 32M(64MB) | 用户文件卷 |
| RESERVE2 | 尾部 | 3M | **尾部边界 = 物理芯片顶**，见 1.3 |

- 原生 App 的现实安装介质：LittleFS 文件（PoC 路径）或 APP 槽（固件路径）。PSRAM 执行区：主固件 4MB @0x90000000（`mem_map.h:42-44`），PoC 执行区 2MB @0x93E00000（`app-host-poc.md` §4）。
- 板型差异：`BOARD_FLASH_SIZE ?= 128`（`Makefile:73`，决定 LittleFS 96M/32M）；`BOARD_PSRAM_SIZE=64` 默认（`Makefile:122`）。64MB PSRAM = 60M 可用窗口，32MB = 28M（`mem_map.h:60-66`）。App Host PoC 执行区**仅在 64MB PSRAM 配置可用**，32MB 构建 `$(error)` fail-closed；链接期 `ASSERT(__psram_bss_end__ <= ORIGIN(APP_HOST_RAM))` 机械防越界（`app-host-poc.md` §4，实测 `__psram_bss_end__=0x93A9EC20`，余量 ≈3.4MB）。【源码/静态分析】+【历史真机】（PoC §5 构建）

### 1.2 擦写粒度与寿命语义

【源码/静态分析】`Custom/Hal/storage.c` / `storage.h`:

- 擦除粒度 4KB 扇区（`storage.h:18` `FLASH_BLOCK_SIZE=4096`；`storage.c:585` `XSPI_NOR_Erase4K`）；NVS 写粒度 4 字节（`storage.h:34`）。LittleFS/NVS 均只用 4K 擦（`storage.c:699`）。
- NOR 编程前做"只能 1→0"检查，违规返回 `LFS_ERR_CORRUPT`（`storage.c:34-42, 81`）。
- 磨损管理：littlefs `block_cycles = max_erase = 10000`（`storage.c:823, 497`）提供卷内均衡；但 `erase_counts[]` 数组**只存 RAM、重启清零**（`storage.c:102-104, 475`），非持久磨损账本——寿命管理是统计性的，不是保证。【推断】
- Memory-mapped 模式处理：所有读/写/擦都临时退出 XSPI MM 模式再恢复（`storage.c:50-58, 76-92, 106-118`）；大擦除（≥128 块）期间锁抢占 + IWDG 喂狗 + 周期让出窗口防 XIP I-cache 逐出与 TCP 超时（`storage.c:560-628`）——意味着**擦写期间任务调度与网络行为有可观测扰动**，是安装事务的时序约束。【源码/静态分析】

### 1.3 LittleFS 持久化/恢复机制与两个关键缺口

【源码/静态分析】`storage.c`:

1. **挂载失败即静默全格式化**：`lfs_mount` 失败 → `lfs_format` + 重挂载（`storage.c:511-515`）。用户数据无任何抢救路径。这与既往真机写入事故后"数据不可保"的表现互洽（见 1.5）。
2. **裸写/裸擦路径无分区越界保护**：`storage_flash_write`（`storage.c:530-542`）与 `storage_flash_erase`（`storage.c:553`）只查 4K 对齐，不查分区边界。`mem_map.h:69-72` 注释明示"RESERVE2_END 是 OTA bundle precheck 强制的上界，其下擦写路径自身不做边界检查"。唯一的上界防御在 bundle 直烧路径（`upgrade_manager.c:216-221`，对照 `RESERVE2_END`）。错误地址的裸写可以越过目标分区破坏相邻数据——这是 API 能力事实，是否构成风险取决于调用方（当前调用方 = NVS/upgrade_manager/bundle，均传常量表地址）。
3. NVS 初始化失败的处理是**擦除该分区并重启**（`storage.c:859-883`）；配置类数据掉电损坏的恢复语义 = 清零重来。

【历史真机】LittleFS 曾发生写入事故，且全量备份被证实不耐久（Issue #25 / ne30x-app device-evidence.md 记录，契约引述）。本调查未复测、也未读到事故根因分析——按"约束"对待而非"已定位问题"。【尚需验证】事故根因。

【历史真机】PoC 期间 LittleFS 96MB 分区可全量读出备份（6×16MB 分块、0 错误），用固件自带 littlefs v2.1 源码离线挂载成功（`block_count=24576`，元数据对迁移至 ≈52.7MB 处），注入 2 个文件仅变更 2 个 4K 块并成功回写回读（`app-host-poc.md` §7.3）。**此为受控单线程离线操作证明，不是生产事务证明**（见 §2.4）。

### 1.4 版本化 Host ABI 与装载执行

【源码/静态分析】`Custom/Common/Inc/app_host_abi.h`:

- `APP_HOST_ABI_VERSION = 0x00010000`，主/次版本提取宏（:11-13）。
- 32 字节镜像头：magic `'NEA1'` / header_size / format_version / abi_version / target_addr / image_size / entry_offset / crc32(payload)（:15-31，`_Static_assert` 锁布局）。
- API 表仅 `{table_size, abi_version, log, tick_ms}`（:38-43）——能力面极小，无内存/线程/IO 授权。

【源码/静态分析】`Custom/Services/AppHost/app_host.c:77-140` 装载流程：LFS 只读读入执行区 → 先 scrub 清零 → 六重预检（magic/头长/格式版本/ABI/target_addr/镜像与入口边界 + CRC32，`app_host_validate`，host 端 21 项测试同源实现）→ 摘头 memmove + 尾部清零 → D-cache clean/invalidate + I-cache invalidate + DSB/ISB → 函数表调用 entry → 捕获返回值 → 再次 scrub 清零（残留代码不留驻）。校验失败：不执行、清零、返回机器可读原因。

【历史真机】装载执行通过：entry=0x93E00000、函数表 `log`/`tick_ms` 调用返回 0x600D；CRC 损坏镜像被拒绝且平台抓拍周期照常；CLI `reset` 重启后 45 秒启动日志 0 条 apphost 行为（默认关闭、无自启循环）（`app-host-poc.md` §7.4）。

【推断】ABI 有版本字段但**无兼容性协商机制**（entry 收到不认识的 abi_version 只能自行拒绝）；API 表按值内联、无独立版本演化路径。

### 1.5 原生 App 对主固件的故障/内存隔离边界

【源码/静态分析】:

- Cortex-M55 **无 MMU**，无地址空间隔离；TrustZone 不在 App 路径上使用（App 运行于非安全/或单态配置，本调查未见 secure/insecure 分区利用——FSBL boot.c 直接 copy+jump，无安全世界交接）。
- MPU 仅配置 2 个区域：`.uncached_bss` 非缓存区、MSP 栈底 256B 哨兵（`Appli/Core/Src/main.c:171-214`），随后 `HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT)`——**默认映射全部特权 RWX**。PSRAM 背景映射可执行（主固件 .text 就在其中，PoC 文档 §4 亦明示）→ 写入 PSRAM 的任何字节默认可执行，且可读写全部外设与内存。
- 结论（如实报告）：**不存在沙箱保证**。原生 App 的野指针写、栈溢出、除零/HardFault 均直接作用于整机；崩溃即整机故障（挂死或经 IWDG 重启），无"App 崩溃平台存活"的性质可承诺。装载前静态校验（PoC 六重预检）只能降低坏镜像被启动的概率，不能兜底运行时故障。【源码/静态分析】+【推断】（崩溃行为本身未经真机构造验证——PoC 明确未做恶意/故障样本测试）

---

## 2. AC2：安装/升级/卸载/重启恢复可行性矩阵

### 2.0 现有事务机制盘点（矩阵的前提事实）

【源码/静态分析】:

| 机制 | 事实 | 锚点 |
|---|---|---|
| APP 槽切换 | 仅 `FIRMWARE_APP` 有 offset_A≠offset_B；WEB/AI/WiFi/FSBL 单槽 A==B | `upgrade_manager.c:9-19` |
| 升级开始 | `upgrade_begin` **先擦目标分区再收数据** | `upgrade_manager.c:194-210` |
| 升级完成 | 切 active_slot + 写 PENDING_VERIFICATION；**写后 CRC 校验代码整段被注释**，slot.crc32 落库值恒为占位 0xFFFFFFFF | `upgrade_manager.c:256-298`（注释块 271-284） |
| 启动确认 | PENDING 槽 try_count>3 → UNBOOTABLE → 切另一槽；boot_success 累计 ≥3 次启动 → ACTIVE | `upgrade_manager.c:346-361, 378-419`；`FSBL/Core/Src/boot.c:115` |
| SystemState | 8K OTA 分区，CRC32 保护；损坏则清零并从槽 A OTA 头重建元数据 | `upgrade_manager.c:36-43, 98-126` |
| State 保存 | **先擦后写、非原子**，擦后写前掉电 = 状态空白 → 走重建路径 | `upgrade_manager.c:36-43` |
| Bundle 升级 | **forced direct burn：无 AB 槽、无 system state**，按预检计划直写绝对地址；三重门（会话 armed + 类型在计划 + 地址=设备自解析值） | `api_ota_module.c:416-418, 435-456, 1052` |
| 文件上传 | temp 兄弟文件 + 末字节落盘后 rename 覆盖目标；上限 16MB（flash/SD 均是）；需会话认证 | `api_file_module.c:182-183, 280-286, 326-341` |
| Host 装载 | 只读装载 + 六重预检 + 执行区边界；无启动接线、无 auto_start | `app_host.c:77-140`；`app-host-poc.md` §2/§7.4 |

### 2.1 矩阵

图例：✅ 一致性目标现有机制可满足；⚠️ 有条件满足/降级满足；❌ 现有机制不满足（阻塞）。

#### S1 APP 固件升级（唯一真 A/B 路径）

| 故障模式 | 结论 | 一致性目标 / 前提 / 缺口 |
|---|---|---|
| 接收中断 | ⚠️ | 目标槽 begin 时已擦空、状态未写，active 槽未动 → 重启旧版本继续跑（✅"设备可用"）。缺口：半新镜像残留在目标槽无清理，依赖下次升级重擦（无害但脏）；【尚需验证】真机中断时序 |
| 掉电/撕裂 | ⚠️ | 写入中途掉电：目标槽半写 + active_slot 未切 → 旧版本继续（✅）。状态页掉电窗口（擦后写前）：SystemState 空 → 重建为"槽 A PENDING + 从 A 头恢复元数据"（`upgrade_manager.c:108-125`）→ 版本选择回退旧版（✅降级），但**升级意图丢失**。最坏组合"状态已切槽 + 镜像半写"：boot 无内容校验（boot.c 直接 copy+jump，写后 CRC 又被注释）→ 半写镜像被启动 → 崩溃循环 → try_count 3 次 → UNBOOTABLE → 回退另一槽（前提另一槽有效）→ 理论闭环但全链未经真机验证。【尚需验证】 |
| 容量不足 | ✅ | 写前拒绝：`validate_partition_size=TRUE`（`api_ota_module.c:455`）+ `upgrade_write_chunk` 超 total 拒绝（`upgrade_manager.c:247`） |
| 无效镜像 | ⚠️ | 完整性可拒（头 CRC32 + payload CRC32，normal 路径 `validate_crc32=TRUE` `api_ota_module.c:451`）；**真实性不可拒**（无签名，见 §3.1）——CRC 匹配的恶意镜像畅通 |
| 重复提交 | ✅ | begin 重擦重写覆盖前者，幂等 |
| 失败回退 | ⚠️ | try_count/UNBOOTABLE/切槽机制存在且结构清晰；前提：另一槽状态 PENDING/ACTIVE 且内容完好、SystemState 未在同时损坏；【尚需验证】真机回退演练 |

#### S2 单槽组件升级（WEB / AI 模型 / WiFi FW）

| 故障模式 | 结论 | 说明 |
|---|---|---|
| 接收中断 | ❌ | begin **先擦唯一（正在使用的）分区**（A==B，`upgrade_manager.c:9-19, 194-210`）→ 中断后该组件数据丢失且无备份 |
| 掉电/撕裂 | ❌ | 同上，写一半掉电 = 该组件损坏，**无回退槽**。设备仍可 boot（App/FSBL 完好，✅设备可用），但功能缺失直到重新完整升级 |
| 容量不足 | ✅ | 同 S1 拒绝先行 |
| 无效镜像 | ⚠️ | 同 S1：完整性可拒、真实性不可拒 |
| 重复提交 | ✅ | 重试覆盖 |
| 失败回退 | ❌ | 单槽无回退语义；WiFi 的 OTA 头甚至不落 flash（`upgrade_manager.c:15-17` 注释），元数据回退也无从谈起 |

#### S3 原生 App 安装（LittleFS + Host 装载，PoC 路径）

| 故障模式 | 结论 | 说明 |
|---|---|---|
| 接收中断 | ✅ | temp+rename：原文件不受影响，partial 由 cleanup 删除（`api_file_module.c:326-341`） |
| 掉电/撕裂 | ⚠️ | littlefs v2.1 COW 元数据 + rename 原子性为设计保证【推断】；但既往真机写入事故 + 全量备份不耐久【历史真机】表明 FS 层理论保证不能直接当真机性质。**禁止把 PoC 的离线注入当成生产事务证明**（见 2.4）。【尚需验证】装机尺寸级掉电测试 |
| 容量不足 | ✅ | 16MB 上限 + 挂载/空间检查先行拒绝（`api_file_module.c:309-315`；free-space 缓存注释表明空间检查留 ≥512KB 余量 `storage.c:637-638`）。【尚需验证】满盘/碎片行为 |
| 无效镜像 | ✅（限装载时） | 六重预检 + 执行区边界 + CRC，失败不执行并清零；CRC 损坏样本真机拒绝、平台存活【历史真机 PoC §7.4】。真实性仍不可拒（CRC 非认证）。前提：镜像 ≤2MB 执行区 |
| 重复提交 | ✅ | 同路径覆盖写，幂等 |
| 失败回退 | ⚠️ | 无版本/槽概念；"回退"=重传旧文件。装载失败返回错误不伤机（✅）；无自动回退旧版本机制 |
| 卸载 | ✅ | `DELETE /api/v1/files?fs=flash&path=...`（`api_file_module.c:815-817`）删除即卸载；执行区运行后清零不留驻（`app_host.c:138`）；重启零自动装载【历史真机 PoC §7.4】。前提：PoC 无持久安装状态，一切靠手动 |
| 重启恢复 | ⚠️ | 平台恢复 ✅（无自启循环已证）；**App 不随重启存活**——auto_start 未实现，属 PoC non-goal。生产安装模型需要的"安装后常驻"【尚需验证/未实现】 |

#### S4 重启恢复（设备级）

| 对象 | 结论 | 说明 |
|---|---|---|
| APP 槽选择 | ⚠️ | try_count/boot_success/切槽如 S1；SystemState 损坏 → 重建默认 SLOT_A（前提槽 A OTA 头有效） |
| FSBL | ❌ | 无槽、无校验、无回退；损坏 = 砖，救援依赖拨码 SW2 烧录模式 + STM32CubeProgrammer【历史真机 PoC §7.0】 |
| LittleFS | ⚠️ | mount 失败 → 静默格式化（`storage.c:511-515`）→ "恢复" = 数据全丢、卷可用 |
| NVS | ⚠️ | init 失败 → 擦除 + 重启（`storage.c:859-883`）→ 配置全丢 |

### 2.4 PoC 注入 ≠ 生产事务证明（契约明确要求）

【推断】PoC §7.3 的文件进入设备方式是：host 端用固件同源 littlefs 源码**离线**修改全分区镜像、受控回写 2 个 4K 块。它不经固件写入路径、无并发、无掉电、无网络栈参与——只证明三件事：LittleFS 卷可离线互操作、文件能进设备、Host 能装载执行。**不证明**：运行时上传事务的原子性、掉电撕裂行为、并发访问一致性。生产安装事务的掉电矩阵全部【尚需验证】。

---

## 3. AC3：设备侧验签、信任锚与执行控制

### 3.1 设备侧验签：不存在

【源码/静态分析】:

- OTA 头结构中安全段 416 字节**全 reserved**（`ota_header.h:121-122`）；`OTA_SIGNATURE_SIZE 256 (RSA-2048)` 常量定义了但结构体无签名字段、全仓库无消费（`ota_header.h:28`）。
- `ota_header_verify` 仅查 magic / header_version / header_size / 头 CRC32（`ota_header.c:18` 起）。
- `ota_validation_options_t.validate_signature` 字段存在，但全仓库唯一赋值点是 `AICAM_FALSE`（`api_ota_module.c:452, 595`）——设备侧 OTA 校验**从不验签**。
- 头内 `fw_hash[32]`（SHA256）字段在设备代码中**零引用**（仅定义于 `ota_header.h:101`；OTA 服务路径无任何 SHA256 调用）。完整性校验上限 = CRC32。
- 打包器侧同样不产出签名：`Script/ota_packer.py` encrypt/compress 写 0、安全段保持全零（:108-137）；`make sign` 用 `STM32_SigningTool_CLI -hv 2.3` 给 FSBL/App 加 **ST 专有头**（`Makefile:218-222`）——那是 ST 启动认证格式，不是设备运行时校验的发行方签名。
- FSBL→App：`boot.c:100-127` 纯 copy+jump，无签名/哈希/CRC 校验。是否存在"ROM authentic boot 验 FSBL"的硬件层链条取决于 option bytes 配置，**源码不可见**【尚需验证】。
- mbedTLS 在固件中真实存在且可用，但仅用于**出站** TLS（webhook CA bundle `webhook_service.c:42,125`、MQTT TLS `mqtt_service.c:834-1035`、AWS SigV4 HMAC `aws_sigv4.c:12-57`）与自测（`tls_test.c`）——即：设备具备验签所需的密码学原语，缺的是签名结构与验证接线。

### 3.2 信任锚现状

【源码/静态分析】:

- **代码签名信任锚：不存在。** 无内嵌公钥、无 NVS 公钥键、无验签代码路径（3.1 全部证据）。
- 出站 TLS 信任锚 = CA bundle（编译期内置 Let's Encrypt 等）+ 用户可经 API 覆盖的 `/certs/webhook_ca.pem`（存 LittleFS，`json_config_nvs.c:553-608`）——该锚本身无完整性保护（FS 格式化即丢，可被任何持管理密码者替换），且与代码安装无关。
- 【推断】若要建信任锚，NVS factory 区（32K，`storage.h:38`）是现有"不随升级变化"的候选位置，但其防篡改依赖 Flash 无裸写 API 暴露（1.3 证明裸写路径存在、无越界保护）——**NVS 不是硬件写保护，只是约定**。

### 3.3 安装授权边界

【源码/静态分析】:

- Web API 认证 = 单管理员**密码**：登录/改密两路由（`api_auth_module.c:107-120`），会话机制存在（upload 路径 401/403 检查 `api_file_module.c:280-286`）。
- 密码安全强度低：自定义 32bit LCG 哈希（`auth_mgr.c` `auth_mgr_hash_password`，非密码学哈希）；密码在 NVS 为明文存储（`json_config_get_device_password` 取回原文）；**init 时明文打印进日志**（`auth_mgr.c:99` `LOG_CORE_INFO("Admin password: '%s'", ...)`）。
- 授权模型推论：**知道管理密码 = 可推送任意固件、直烧任意分区、写任意文件**。bundle 直烧有三重会话门（2.0 表），但那是防错序不是防恶意——无验签前提下无差别。首次启动强制改密行为在本会话源码中未定位到实现（`json_config_nvs.c:1655+` 有 is_first_boot 逻辑但与密码强制的关联未核实）【尚需验证】。

### 3.4 代码执行控制

【源码/静态分析】+【历史真机】:

- 当前唯一执行入口：编译期开关 `APP_HOST_POC ?= 0`（默认关闭，flag=0 构建与基线逐位一致）+ UART CLI 手动 `apphost load`（`app_host.c:142-165`；PoC §3/§7.4）。重启后零自动执行【历史真机 PoC §7.4】。
- CRC32 仅完整性、**不是发行方认证**——PoC 文档 §3 已明示，本调查确认无任何被误当认证的机制。
- 硬件层无执行控制可依赖：MPU 默认特权 RWX（1.5），PSRAM 可执行，`DisableExec` 仅用于 256B 栈哨兵。**任何能写入内存的代码都可被执行；任何被执行的代码都是特权代码。**
- 入站通道盘点：UART ymodem = TODO 桩（`debug.c:811-815`），且 YMODEM 模式下 UART ISR 丢输入、不可恢复仅重启可退【历史真机 PoC §7.3/§8】；Web 上传/OTA = 认证后的文件与固件写入（上）；MQTT/Webhook = **出站** client（3.1），无入站代码推送通道的证据；设备不在可达局域网时这些网络通道全部不可用【历史真机 PoC §7.3】。

### 3.5 最小可行选项与不可承诺性质（证据性陈述，非决策）

【推断】基于现有资产的最小验签路径：

1. OTA 头安全段（416B reserved）足够容纳 ECDSA P-256 签名 + 证书/公钥指纹结构——格式空间现成，设备端 mbedtls 原语现成（3.1），缺的只是：打包器写签名、`ota_header_verify` 或 `ota_service` 增加验签步、信任锚持久化位置（NVS factory 候选）与更新策略。
2. App 镜像（Host ABI）可复用同一信任机制：32B 头后接签名块即可，六重预检扩展为"验签 + CRC"。
3. 安装授权：现有会话机制可沿用；密码存储/日志泄漏应作为独立整改项记录（3.3）。

**不可承诺的隔离性质（如实报告）**：内存隔离、故障隔离（App 崩溃平台存活）、特权降级、运行时执行域限制——在 M55 无 MMU + 当前 MPU 默认 RWX 配置下均不存在，且不是"加验签"能解决的；需要 MPU 分区方案 + HardFault 恢复策略 + 看门狗语义另行设计与验证，当前无任何一处有证据支持。

---

## 4. 历史真机事实汇总（本会话全部引述，未复测）

| 事实 | 来源 |
|---|---|
| LittleFS 曾发生写入事故，全量备份被证实不耐久 | 契约引述 Issue #25 / ne30x-app device-evidence.md |
| ymodem 接收为 TODO 桩；YMODEM 模式 UART 不可恢复，仅重启可退 | 【源码】`debug.c:811-815` +【历史真机】PoC §7.3 |
| 设备不在可达局域网时无远程通道（上传/OTA 均不可用） | 【历史真机】PoC §7.3 |
| App Host PoC 真机：装载执行 0x600D、CRC 损坏拒绝、重启零自动装载 | 【历史真机】PoC §7.4 |
| App1/OTA state 可字节级备份还原；LittleFS 残留 2 个 152B 测试文件未还原 | 【历史真机】PoC §7.2/§7.5 |
| 真机读写须拨码 SW2 烧录位（SWD/UR 正常位不可达外存） | 【历史真机】PoC §7.0 |
| LittleFS 96MB 全量读出 0 错误；离线 littlefs 挂载/注入/2 块回写成功 | 【历史真机】PoC §7.3 |

---

## 5. 未知 / 尚需验证清单

1. **OTA APP 升级真机掉电矩阵**：尤其 `save_system_state` 擦写窗口掉电、双槽状态组合（状态已切+镜像半写）下的回退闭环——源码结构支持但从未验证。
2. **单槽组件（WEB/AI/WiFi）升级中断后的整机行为**：App 是否完全存活、组件缺失的降级表现——无真机证据。
3. **LittleFS 真机掉电撕裂行为**（装机尺寸级文件）与**既往写入事故根因**（device-evidence.md 原文本会话未读，仅契约引述级）。
4. **ROM authentic boot / option bytes 配置状态**：FSBL 是否有硬件层验签链，决定"信任锚是否已有硬件根"。
5. **NVS factory 32K 内容与余量**：PoC 期间从未读取（内容未知，PoC §8 明示）——评估其作为信任锚位置的前提。
6. **满盘/碎片化 LittleFS 的 16MB 上限文件写入行为**与 SD 卡（FS_SD）路径和 flash 路径的事务性差异。
7. **32MB PSRAM / 64MB 闪存板型的实际装机分布**：PoC 执行区仅 64MB PSRAM 可用，32MB 板型被 fail-closed 禁止——影响安装模型覆盖面。
8. **首次启动强制改密的实现位置**：`is_first_boot` 逻辑与密码强制的关系未核实（3.3）。
9. **bundle 直烧在错表（但 CRC 自洽）场景下的分区保护边界**：precheck 有 RESERVE2_END 上限 + 分区表 CRC（`ota_header.h:105-110`），但错表是合法包——无验签前提下与恶意包无区别（已在 3.1 定性，残余未知是"错表实际能破坏到哪"）。
10. **Host ABI 演化机制**：版本协商、API 表扩展（线程/回调/订阅）对隔离性的影响——PoC 明确未覆盖（PoC §8）。
11. **Host 装载路径下恶意/故障样本的实际整机后果**（构造 HardFault 的镜像）：验证 1.5 "崩溃即整机故障"的精确表现（挂死 vs IWDG 重启 vs 需断电）。
