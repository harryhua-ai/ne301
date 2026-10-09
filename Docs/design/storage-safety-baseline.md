# 存储安全基线：LittleFS / NVS 非破坏性启动与故障恢复（Issue #37）

状态：已实现（实验分支 `agent/37/ef9ccc5f`，基线 `c97ee19b`）
验证边界：**仅 host 错误注入回归 + 目标固件编译**。零真机操作（无刷写、无格式化、无掉电实验）。

---

## 1. 背景与问题

#27 调查（`Docs/design/app-install-feasibility.md` §1.2/§2.1，行 174）已记录两个存储风险：

1. **LittleFS 静默格式化**：`storage.c` 的 `lfs_mem_init` 在 `lfs_mount` 失败后无条件
   `lfs_format` + 重挂载。#2 事故（885 个文件丢失）中"静默格式化即恢复"是该链条的
   最终失效模式；固件侧根因虽未被独立复现，但该代码路径本身就是把"状态不明的卷"
   当作可丢弃对象。
2. **NVS 故障擦除重启**：`storage_init` 在 factory/user NVS 初始化失败时擦除对应
   分区并重启。管理员凭据迁移（#36）、设备配置、出厂 SN/MAC 全暴露在"一次初始化
   失败 = 全部清空"的风险下。
3. **上层默认回写**：`json_config_mgr_init` 在配置加载失败后把默认配置写回 NVS；
   `json_config_load_from_nvs` 仅凭 `NVS_KEY_MAGIC_NUMBER` 缺失/不符判定
   `is_first_boot` 并写默认值——magic 缺失不是空白证明，可能把旧数据覆盖成默认值。
4. **格式化假报成功**：`storage_format()` 返回 `void`，`POST /device/storage/format`
   无论底层成败都构造成功响应。

共同 WHAT：**存储故障不得静默擦除已有或状态不明的用户数据/配置**。

## 2. 三态不变量（A 决定）与实现映射

| 不变量 | 实现位置 | 行为 |
|---|---|---|
| ① 已可信挂载/读取的旧卷与配置 → 原功能继续 | `storage_lfs_probe_and_mount()`：mount 成功即 `STORAGE_LFS_STATE_OK`，不写盘；`json_config_boot_classify()`：magic 有效即 `PERSISTED`，per-key 新键回填（legacy 迁移）保持原行为 | 正常路径零行为变化（注入测试 `test_healthy_volume_mounts_and_keeps_files`、`test_nvs_blank_init_writes_no_erase_and_roundtrips` 证明） |
| ② 可独立确认的首次空白介质 → 仅经明确受权的初始化路径写入 | LittleFS：probe 只读空白证明（全卷 0xFF 校验）→ `STORAGE_LFS_STATE_NEEDS_INIT`，**不写盘**，唯一写入路径 = 显式 `storage_format()`；NVS_USER：`storage_nvs_blank_check()` 只读全分区擦除校验 + magic 缺失 → `FIRST_BLANK`，此时首次默认初始化可运行（写入是纯追加，对已证明擦空的介质无破坏） | "空白"必须有**完整读回证明**，mount 失败/magic 缺失单独不构成证明（`test_read_failure_is_unavailable_never_blank`、`test_boot_policy_matrix`） |
| ③ 状态不明/读取失败/损坏 → 保留介质 + 报告不可用，fail-closed | LittleFS：`UNAVAILABLE`（media-preserved）；NVS init 失败：分区保留、`storage_nvs_ready()=false`、所有访问器 `-EACCES`；配置层：`UNRECOGNIZED`/`BACKEND_UNAVAILABLE` → 本会话 `persist_blocked`，全部持久化拒绝，RAM 默认值仅运行时生效 | 注入测试断言拒绝路径上 erase/prog 计数为 0 且介质逐字节不变（`snapshot_diff_is_zero`） |

**证据不足时默认拒绝初始化**：本实现没有"独立空白证明 + 独立授权"双要件中
"独立授权"机制（无出厂签名空白证明、无独立初始化授权流），因此：
- LittleFS NEEDS_INIT：设备保持"需要安全初始化"状态直到管理员调用显式格式化 API
  （现有 `POST /device/storage/format`，`require_auth=true`；认证强化属 #36）。
- NVS_USER proven-blank 首启：视为出厂首次初始化的受控路径（写入对象已被证明是
  纯擦空空间，操作不可造成信息丢失）；**生产出厂流程（烧站写 SN/MAC/校准后再
  出货，或显式出厂初始化授权）属 A/User 后续决策**，本任务不改变面向用户的
  出厂体验承诺。

## 3. 改动清单（按层）

### 3.1 新增：`Custom/Hal/storage_safety.{c,h}`（硬件无关、host 可测）
- `storage_lfs_probe_and_mount()`：mount + 只读分类（OK / NEEDS_INIT / UNAVAILABLE），
  永不擦写。
- `storage_lfs_volume_blank()`：经 `lfs_config.read` 的全卷 0xFF 校验；读失败一律
  返回"不可用"，绝不误报空白。
- `storage_lfs_format_volume()`：唯一格式化路径——unmount（尽力）→ format →
  remount，返回真实结果。
- `storage_blank_check_range()`：可注入 read 函数的原始区域擦除校验（NVS 用）。

### 3.2 `Custom/Hal/storage.{c,h}`
- `lfs_mem_init`：删除 `mount 失败 → lfs_format → 重挂载`（原 509-519 行）；改为
  probe + 状态记录 + 错误日志。`storage_t` 增加 `lfs_state / lfs_mount_err /
  nvs_fact_err / nvs_user_err` 诊断字段。
- `storage_init`：删除 factory/user NVS 失败 → `storage_flash_erase` + U0 复位 +
  `HAL_NVIC_SystemReset` 的路径（原 859-883 行）；改为记录错误、分区保留、继续启动。
- `storage_format()`：`void` → `int`（0 = 格式化且重挂载均成功；负数 = 真实失败，
  卷保持 unmounted）。语义仍是**显式、破坏性、管理员授权**的入口（本任务不废止）。
- 新增门面查询：`storage_get_lfs_state()`、`storage_nvs_ready(type)`、
  `storage_nvs_blank_check(type, &blank)`。
- 依赖方 fail-closed 现状核对：未挂载时 `flash_lfs_fopen/opendir/remove/rename/
  stat/mkdir` 经 `storage_lfs_isready()` 返回 NULL/-1；`fread/fwrite/fflush` 只能
  作用于已打开句柄（未挂载时拿不到句柄）；`storage_get_disk_info()` 如实报告
  `mounted=false`；`storage_nvs_read/write/delete/clear` 在分区未就绪时返回
  `-EACCES`/`-1`（NVS 库 ready 检查）。无新增假成功路径。

### 3.3 新增：`Custom/Core/System/json_config_boot_policy.{c,h}`（纯逻辑、host 可测）
- `json_config_boot_classify()`：backend_ready × blank_check_err × blank ×
  magic 证据 → `PERSISTED / FIRST_BLANK / UNRECOGNIZED / BACKEND_UNAVAILABLE`。
- `json_config_boot_allow_persist()` / `allow_first_boot_init()`：状态 → 是否允许
  任何 NVS 持久化 / 首启默认初始化。

### 3.4 `Custom/Core/System/json_config_nvs.c`
- `json_config_load_from_nvs`：加载前先采证（`storage_nvs_ready(NVS_USER)` +
  `storage_nvs_blank_check` + magic 读取），交 policy 分类：
  - `PERSISTED`：原路径（含 per-key 新键回填、invariant 修正写回）。
  - `FIRST_BLANK`：原首次初始化路径（写默认值）——仅对已证明擦空分区。
  - `UNRECOGNIZED` / `BACKEND_UNAVAILABLE`：**零写入**直接返回
    `AICAM_ERROR_CORRUPTED` / `AICAM_ERROR_UNAVAILABLE`。
- fail-closed 持久化门：
  - `json_config_save_to_nvs()` 顶部 choke：blocked 时拒绝并返回
    `AICAM_ERROR_UNAVAILABLE`（覆盖全量保存、恢复出厂、备份等所有全量路径）。
  - 全部 7 个 per-key 写助手（string/uint32/uint64/float/uint8/bool/int32）顶部
    门控 —— 覆盖所有 `json_config_set_*` 分散写路径。
  - `json_config_save_isp_config_to_nvs()` 单独门控 —— ISP 校准 blob 走
    `storage_nvs_write` 直写，防止降级会话用零值覆盖出厂校准。
- magic 常量收敛为 `NVS_CONFIG_MAGIC_NUMBER`（`json_config_internal.h`）。

### 3.5 `Custom/Core/System/json_config_mgr.{c,h}` + `json_config_internal.h`
- `json_config_mgr_init`：加载失败 → `json_config_load_default()` 仅写 RAM 副本
  （并补齐 `capture_upload` 等静态表外字段），**不写 NVS**；置
  `persist_blocked` + `degraded_reason`；**返回 AICAM_OK**（带降级状态继续启动，
  避免 core_init 失败 → 重启循环；原实现 load 失败且 save 失败会向上报错）。
  MAC 设备名生成及保存仅在非降级路径执行。
- `json_config_mgr_deinit`：降级会话跳过收尾保存（RAM 里是默认值，写回即覆盖）。
- 新增诊断 API：`json_config_mgr_persist_blocked()`、
  `json_config_mgr_degraded_reason()`。
- `json_config_mgr_context_t` 增加 `persist_blocked / degraded_reason`。
- 已知残余：降级会话期间管理员密码表现为默认值且敏感设置写不进持久层（安全拒绝
  但登录面回退到默认凭据）——这是"保留数据"与"可用性"的取舍，记录给 #36 认证
  强化时统一处理。

### 3.6 `Custom/Services/Web/api/api_device_module.c`（唯一 Web 层触点）
- `device_storage_format_handler`：`storage_format()` 失败时返回
  `API_ERROR_INTERNAL_ERROR`（"Flash format failed; storage left unchanged"），
  不再构造成功响应；`upload_coordinator_reload_config()` 仅在成功后执行。
  未动认证（#36 范围）、未动 Web 前端。

### 3.7 显式入口保留清单（非 goal：不废止破坏性管理能力）
- `storage_format()` / `POST /device/storage/format`（ LittleFS 显式格式化，现在
  结果真实可核实）。
- `storage_nvs_clear()`（NVS 显式清空 API，现无调用方，保留供显式恢复流程）。
- 恢复出厂（`json_config_reset_to_default`）：正常会话行为不变；降级会话中被
  save-choke 拒绝（fail-closed）。

## 4. 错误注入回归（AC4，host 端）

位置：`tests/storage_safety/`（`make -C tests/storage_safety test`）。
链接**固件同源真实代码**：`littlefs`（`Custom/Common/Lib/littlefs`）、NVS 库
（`Custom/Common/Lib/nvs/nvs.c`）、`storage_safety.c`、`json_config_boot_policy.c`，
外加故障注入 RAM flash（`fake_bd.c`：读/写失败注入、erase/prog 计数、全盘快照
比对）。littlefs 的 `Hal/mem.h`、`crc.h` 依赖以 host shim 提供（沿用
`tests/app_host/lfstool/shim` 模式）。

| # | 场景（注入） | 断言 | 结果 |
|---|---|---|---|
| 1 | 空白卷 mount 失败 | 分类 NEEDS_INIT；erase/prog 计数=0；全盘逐字节不变 | PASS |
| 2 | 损坏卷（破坏 superblock，非空白） | 分类 UNAVAILABLE（非 NEEDS_INIT）；erase/prog=0；逐字节不变 | PASS |
| 3 | 读失败（首读即 IO 错误） | 分类 UNAVAILABLE——**不可读永不判空白**；erase/prog=0；逐字节不变 | PASS |
| 4 | 健康卷 + 已有文件 | 分类 OK；文件原样可读（正常路径兼容） | PASS |
| 5 | NEEDS_INIT 卷显式格式化 | 返回 0；卷可挂载可写（受权初始化路径有效） | PASS |
| 6 | 健康卷显式格式化 | 返回 0；旧文件确实消失（破坏性真实发生） | PASS |
| 7 | 格式化期间写失败注入 | 返回非 0（真实失败）；卷不处于 mounted | PASS |
| 8 | `storage_blank_check_range` 矩阵 | 空白/单字节翻转/区间外/读失败/参数错误 五分支 | PASS |
| 9 | `storage_lfs_volume_blank` 直测 | 空白 true / 翻转 false / 读失败报错 / NULL 参数拒绝 | PASS |
| 10 | 配置 boot policy 全矩阵（7 输入组合） | 分类与 allow_persist/allow_first_boot_init 全部符合 §2 表 | PASS |
| 11 | NVS 空白首挂（真实 NVS 库） | init 成功 ready=true；**erase 计数=0**；读写改删回环正常 | PASS |
| 12 | NVS 读失败注入后重新 init | init 失败、ready=false；erase/prog=0；分区逐字节不变；read/write/delete/clear 全部 `-EACCES` fail-closed | PASS |

汇总：**116 checks, 0 failures**（固件第三方库文件带 `-Wno-sign-compare` +
`-include stddef.h` 的 host 移植 shim，一方的决策逻辑与测试在
`-Wall -Wextra -Werror` 下编译）。

### 无法 host 化、仅由目标构建 + 静态分析覆盖的部分
- `storage.c` 的 `lfs_mem_init` / `storage_init` 接线（RTOS mutex、XSPI 回调）：
  改动为"删除擦除/重启调用 + 换用已测的 probe/format 函数"，结构上不存在残余
  擦除路径（全仓 `lfs_format` 仅存于 `storage_lfs_format_volume`，见 §3.2）。
- `json_config_nvs.c` / `json_config_mgr.c` 的门控接线：决策表本身已注入测试；
  接线正确性靠编译证据 + 代码审查。per-key 写助手共 7 处门控、全量保存 1 处
  choke、ISP 保存 1 处门控，均有调用链核对。

## 5. 目标构建证据（AC4）

命令：`make app`（worktree 基线 `c97ee19b` + 本改动，工具链
arm-none-eabi-gcc 15.2.Rel1）。

结果：**编译、链接通过，0 warning**，产物 `build/ne301_App.elf/.bin/.hex`。
内存报告（改动后 / 改动前，来自同环境两次构建的链接器输出）：

| Region | 改动后 | 改动前 | Δ |
|---|---|---|---|
| AXISRAM1_2_S | 3,844,484 B (91.68%) | 3,843,052 B (91.65%) | +1,432 B |
| SRAM_POOL | 902,944 B (50.79%) | 902,432 B (50.77%) | +512 B |
| AXI_SRAM_UNCACHED | 189,728 B (59.39%) | 189,728 B (59.39%) | 0 |
| PSRAM | 57,273,376 B (91.03%) | 57,273,376 B (91.03%) | 0 |

text 3,434,336 B / data 410,132 B / bss 58,366,040 B。增量主要来自两个新模块
（状态名表、探针函数）与诊断字段。

## 6. 真机遗留清单（本任务**未验证**，需要单独设备授权）

1. 真机 LittleFS 固件写路径掉电撕裂行为（#27 §2.4 已列为开放项；#2 事故的机制
   未独立复现）。
2. 真机上人为损坏 NVS/LittleFS 后的启动日志与 `storage_get_lfs_state()` /
   `json_config_mgr_persist_blocked()` 的实际表现（含 Web 界面上的可用性降级）。
3. 真机 96MB 卷的空白探针耗时（`storage_lfs_volume_blank` 全卷 memcpy 读，仅在
   mount 失败路径执行；预计几十毫秒级，未实测）。
4. NEEDS_INIT 状态下管理员经 Web API 显式格式化的端到端流程（含 `require_auth`
   现状下的安全性评估——#36）。
5. NVS_USER proven-blank 首启流程在生产设备上的行为（与出厂烧站流程的衔接，
   A/User 决策）。
6. 降级会话的 Web 交互体验（哪些功能应显式提示"存储不可用"，当前仅日志 + 拒绝
   写入，无 UI 提示——Web UI 不在本任务 scope）。
7. 显式格式化时卷上仍有打开文件句柄的边界（`storage_format()` 会先 unmount 再
   format，比旧实现"挂载状态下就地 format"更安全；但外部已打开的文件句柄在
   remount 后悬置属固有边界，需上层保证格式化前无活跃写入方，或由 #36 的
   管理流程统一约束）。

## 7. 与 #27 / #36 / #30 的衔接

- **#27**：本文档关闭 `app-install-feasibility.md` 行 174 记录的 "mount 失败 →
  静默格式化" 缺口；行 266 的固件写路径掉电项仍开放（§6.1）。App 安装事务
  （P4 #30）现在可以依赖"安装介质不会被启动路径格式化"这一前置保证。
- **#36**（管理员密码/NVS 记录迁移）：迁移的前提"固件不会擦空 NVS"由本次
  `storage_init` 改造满足；本任务暴露的降级会话默认密码问题移交 #36 统一设计。
- **#30**（App 升级事务）：本任务不处理安装事务原子性；仅保证存储启动语义
  不再是事务的破坏源。

## 8. 范围与边界重申

- 未修改：`FSBL/**`、`Frontend/**`、`Model/**`、`WakeCore/**`、Web 前端逻辑
  （除 `api_device_module.c` 的格式化结果处理）、Flash 分区表。
- 未做：真机刷写/格式化/擦除/复位/掉电实验；`ght` 写板；push/PR。
- 生产出厂初始化流程变更不在本 Issue 实施许可内，见 §2 末段。
