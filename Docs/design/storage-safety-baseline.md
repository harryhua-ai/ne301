# 存储安全基线：LittleFS / NVS 非破坏性启动与故障恢复（Issue #37）

状态：已实现 rev 2（实验分支 `agent/37/ef9ccc5f`，基线 `c97ee19b`；rev 2 按 PR #38
review 的 3 项 BLOCKER + 1 项验证缺口修订）
验证边界：**仅 host 错误注入回归 + 目标固件编译**。零真机操作（无刷写、无格式化、
无掉电实验）。

---

## 1. 背景与问题

#27 调查（`Docs/design/app-install-feasibility.md` §1.2/§2.1，行 174）已记录两个存储风险：

1. **LittleFS 静默格式化**：`storage.c` 的 `lfs_mem_init` 在 `lfs_mount` 失败后无条件
   `lfs_format` + 重挂载。#2 事故（885 个文件丢失）中"静默格式化即恢复"是该链条的
   最终失效模式。
2. **NVS 故障擦除重启**：`storage_init` 在 factory/user NVS 初始化失败时擦除对应
   分区并重启。
3. **上层默认回写**：`json_config_mgr_init` 在配置加载失败后把默认配置写回 NVS；
   `json_config_load_from_nvs` 仅凭 magic 缺失判 `is_first_boot` 并写默认值。
4. **格式化假报成功**：`storage_format()` 返回 `void`，API 无论成败都报成功。

rev 2 追加（review BLOCKER 1）：上述第 3 项还有一个更尖锐的后果——配置 NVS 损坏/
不可读时，RAM 里的**编译期默认密码会成为有效管理员凭据**，可达全部敏感 Web API。

共同 WHAT：**存储故障不得静默擦除已有或状态不明的用户数据/配置，也不得把降级
状态误报为可用或可信**。

## 2. 三态不变量（A 决定）与实现映射（rev 2）

| 不变量 | 实现位置 | 行为 |
|---|---|---|
| ① 已可信挂载/读取的旧卷与配置 → 原功能继续 | `storage_lfs_probe_and_mount()` mount 成功即 `OK` 不写盘；`json_config_boot_classify()` magic 有效即 `PERSISTED`，per-key 新键回填保持原行为；凭据用 NVS 存的真实密码 | 正常路径零行为变化（注入测试 `test_healthy_volume_mounts_and_keeps_files`、`test_nvs_blank_init_writes_no_erase_and_roundtrips`） |
| ② 可独立确认的首次空白介质 → 仅经明确受权的初始化路径写入 | LittleFS：只读空白证明 → `NEEDS_INIT`，唯一写入路径 = 显式 `storage_format()`（现要求知情确认）；NVS_USER：全分区只读擦除校验证明空白 → **`PENDING_INIT`（rev 2）**——**不再自动写默认配置**，保持待初始化、拒绝持久写入，直到显式受权动作（出厂复位 API，require_auth 路由）执行首次初始化；与 LFS 侧 NEEDS_INIT 语义一致 | "空白"必须有完整读回证明；mount 失败/magic 缺失单独不构成证明；**空白证明本身也不是写入授权**（review BLOCKER 2） |
| ③ 状态不明/读取失败/损坏 → 保留介质 + 报告不可用，fail-closed | LittleFS：`UNAVAILABLE`；NVS init 失败：分区保留、not-ready、访问器 `-EACCES`；配置层：`UNRECOGNIZED`/`BACKEND_UNAVAILABLE` → `persist_blocked`，全部持久化拒绝；**且 `credentials_trusted=false`，管理认证整体拒绝**（review BLOCKER 1） | 注入测试断言拒绝路径 erase/prog=0 且介质逐字节不变；凭据链负例测试断言认证拒绝 |

**首次初始化的授权现状（rev 2 定案）**：本路径无"独立授权机制"，因此空白 NVS 不再
在普通启动时自动初始化。当前唯一受权入口 = 现有出厂复位操作
（`POST /system/factory-reset` 路由 → `device_service_reset_to_factory_defaults()` →
`json_config_reset_to_default()`）：require_auth 的显式管理动作，仅在介质**已证明
空白**（PENDING_INIT）时放行首次写入；UNRECOGNIZED/BACKEND_UNAVAILABLE 仍拒绝。
空白设备的引导顺序：默认凭据登录（介质已证明为空，出厂默认即其真实凭据）→ 显式
出厂复位完成初始化。生产出厂/烧站流程是否调整属 A/User 后续决策，本实现不改变
已出货设备的升级路径（已有持久化配置的设备走不变量①）。

## 3. 改动清单（按层）

### 3.1 `Custom/Hal/storage_safety.{c,h}`（硬件无关、host 可测）
- `storage_lfs_probe_and_mount()`：mount + 只读分类（OK / NEEDS_INIT / UNAVAILABLE），
  永不擦写。
- `storage_lfs_volume_blank()`：经 `lfs_config.read` 的全卷 0xFF 校验；读失败一律
  返回"不可用"，绝不误报空白。
- `storage_lfs_format_volume()`：唯一格式化路径——unmount（尽力）→ format →
  remount，返回真实结果。**失败语义（review BLOCKER 3）如实定义**：失败的
  lfs_format 可能已擦写部分介质——失败时卷保持 unmounted，介质状态/完整性
  **未被证明**，调用方不得声称数据未受影响。
- `storage_blank_check_range()`：可注入 read 函数的原始区域擦除校验（NVS 用）。

### 3.2 `Custom/Hal/storage.{c,h}`
- `lfs_mem_init`：删除 `mount 失败 → lfs_format → 重挂载`；改为 probe + 状态记录 +
  错误日志（此路径确实零写入，"media preserved" 陈述有据）。
- `storage_init`：删除 NVS 失败 → 擦分区 + 复位重启；改为记录错误、分区保留、
  继续启动。`storage_t` 增加 `lfs_state / lfs_mount_err / nvs_fact_err / nvs_user_err`。
- `storage_format()`：`void` → `int`，真实结果。失败日志（review BLOCKER 3）：
  `"storage_format FAILED … volume left unmounted; media state/integrity NOT
  verified (partial erase possible)"` —— 不再出现无据的 "media preserved"。
- 门面查询：`storage_get_lfs_state()`、`storage_nvs_ready(type)`、
  `storage_nvs_blank_check(type, &blank)`。
- 依赖方 fail-closed 核对：未挂载时 `flash_lfs_*` 门面返回 NULL/-1；NVS 访问器
  在分区未就绪时 `-EACCES`。

### 3.3 `Custom/Core/System/json_config_boot_policy.{c,h}`（纯逻辑、host 可测）
- `json_config_boot_classify()`：证据 → `PERSISTED / PENDING_INIT /
  UNRECOGNIZED / BACKEND_UNAVAILABLE`。
- `json_config_boot_allow_persist()`：**仅 PERSISTED 允许**（rev 2：空白 ≠ 写入
  授权）。
- `json_config_boot_allow_admin_auth()`（rev 2，review BLOCKER 1）：PERSISTED
  （真实凭据可读）与 PENDING_INIT（介质已证明为空，出厂默认即真实凭据）允许；
  UNRECOGNIZED / BACKEND_UNAVAILABLE **拒绝**。

### 3.4 `Custom/Core/System/json_config_nvs.c`
- `json_config_load_from_nvs`：加载前采证（backend ready + 只读空白校验 + magic），
  交 policy 分类：
  - `PERSISTED`：原路径（per-key 回填、invariant 修正写回）。
  - `PENDING_INIT`：**零写入**返回 `AICAM_ERROR_NOT_INITIALIZED`（rev 2：自动
    首启初始化已删除）。
  - `UNRECOGNIZED` / `BACKEND_UNAVAILABLE`：零写入返回
    `AICAM_ERROR_CORRUPTED` / `AICAM_ERROR_UNAVAILABLE`。
- fail-closed 持久化门：`json_config_save_to_nvs` choke + 全部 7 个 per-key 写
  助手 + `json_config_save_isp_config_to_nvs`（防零值覆盖出厂校准）。

### 3.5 `Custom/Core/System/json_config_mgr.{c,h}` + `json_config_internal.h`
- `json_config_mgr_init`：加载失败 → RAM 默认值、**不写 NVS**、`persist_blocked`；
  并按 review BLOCKER 1 置 `credentials_trusted`：仅 PENDING_INIT 为 true
  （介质已证明为空），UNRECOGNIZED/UNAVAILABLE 为 false。返回 AICAM_OK 继续启动
  （避免 core_init 失败重启循环），降级状态可经诊断 API 与日志辨识。
- **凭据门（review BLOCKER 1）**：`json_config_get_device_password()` 在
  `credentials_trusted=false` 时拒绝返回（RAM 副本持有的是编译期默认值，交出即
  等于提升为有效管理员凭据）。
- `json_config_reset_to_default()`（review BLOCKER 2 显式初始化路径）：会话处于
  PENDING_INIT（介质已证明空白）时，本次显式出厂复位即受权首次初始化——解除
  persist_blocked 并写默认值；其余 blocked 状态（UNRECOGNIZED/UNAVAILABLE）仍拒绝。
- `json_config_mgr_deinit`：降级会话跳过收尾保存。
- 诊断 API：`json_config_mgr_persist_blocked()`、`json_config_mgr_credentials_trusted()`、
  `json_config_mgr_degraded_reason()`；ctx 增加 `credentials_trusted`、
  degraded 枚举增加 `JSON_CONFIG_DEGRADED_PENDING_INIT`。

### 3.6 `Custom/Core/Security/auth_mgr.c`（review BLOCKER 1 接线，依赖方 fail-closed）
- `auth_mgr_init`：config stage 先于 security stage（`core_init.c` stage 2 → stage 6），
  故 `json_config_mgr_credentials_trusted()` 在此可信。凭据源不可信时：
  `credentials_untrusted=true`，**不物化默认密码哈希**、不打印密码材料，显式日志
  "admin authentication DISABLED this session"。
- `auth_mgr_verify_password()`：不可信时无条件拒绝（`AICAM_FALSE`）。这一处覆盖
  全部敏感认证消费方：`web_server.c auth_verify_user()`（敏感 API Basic 认证）、
  `api_auth_module.c` 登录端点（会话认证）、`api_file_module.c` 文件 API 认证。
- `auth_mgr_change_password()`：不可信时拒绝（防止内存里铸造未经存储态验证的新
  管理员凭据；该状态下持久化本就被阻断，改密也无法跨重启存活）。
- 不重做 #36 的口令存储/日志/会话全面方案；此处仅为存储降级状态的安全拒绝。

### 3.7 `Custom/Services/Web/api/api_device_module.c`（Web 层唯二触点）
- `device_storage_format_handler`（review BLOCKER 3）：
  - **知情确认**：请求体必须为 `{"confirm":"FORMAT"}`（命名操作的显式确认）；
    缺失/不符返回 400 级错误，错误消息写明目标（内部 flash LittleFS 卷）与后果
    （永久删除日志/抓拍/上传资产，不可撤销）。这是现有 API 语义内的最小确认；
    更完整的 User-owned UX 交互已回交 A，未在本任务实现。**注意：这是破坏性
    API 变更**——现有 Web 前端未发送 confirm 字段，更新前端不在本任务范围。
  - **失败消息真实化**：格式化失败返回错误，消息为"format FAILED – volume left
    unmounted and its state/integrity is NOT verified (the attempt may have
    partially erased the volume)"，不承诺旧数据未受影响。
  - 成功才执行 `upload_coordinator_reload_config()`。认证强化仍属 #36。

### 3.8 显式入口保留清单（非 goal：不废止破坏性管理能力）
- `storage_format()` / `POST /device/storage/format`：保留，现要求知情确认且结果
  真实可核实。
- `storage_nvs_clear()`：显式 NVS 清空 API，保留（现无调用方）。
- 出厂复位（`json_config_reset_to_default` / `POST /system/factory-reset`）：正常
  会话行为不变；PENDING_INIT 会话中成为受权首次初始化入口；UNRECOGNIZED/
  UNAVAILABLE 中拒绝（fail-closed）。

## 4. 错误注入回归（AC4，host 端）

位置：`tests/storage_safety/`（`make -C tests/storage_safety test`）。
链接固件同源真实代码：littlefs、NVS 库、`storage_safety.c`、
`json_config_boot_policy.c`，故障注入 RAM flash（读/写失败注入、erase/prog 计数、
全盘快照比对）。littlefs 的 `Hal/mem.h`、`crc.h` 以 host shim 提供。

| # | 场景（注入） | 断言 | 结果 |
|---|---|---|---|
| 1 | 空白卷 mount 失败 | 分类 NEEDS_INIT；erase/prog=0；逐字节不变 | PASS |
| 2 | 损坏卷（破坏 superblock，非空白） | UNAVAILABLE（非 NEEDS_INIT）；erase/prog=0；逐字节不变 | PASS |
| 3 | 读失败（首读即 IO 错误） | UNAVAILABLE——不可读永不判空白；erase/prog=0；逐字节不变 | PASS |
| 4 | 健康卷 + 已有文件 | OK；文件原样可读（正常路径兼容） | PASS |
| 5 | NEEDS_INIT 卷显式格式化 | 返回 0；卷可挂载可写 | PASS |
| 6 | 健康卷显式格式化 | 返回 0；旧文件确实消失 | PASS |
| 7 | **格式化期间写失败注入（rev 2 强化）** | 返回非 0；unmounted；**erase 计数>0 且介质已被修改**——编码"失败格式化可能部分擦除"，证明任何"unchanged/preserved"陈述均为虚假 | PASS |
| 8 | `storage_blank_check_range` 矩阵 | 空白/单字节翻转/区间外/读失败/参数错误 | PASS |
| 9 | `storage_lfs_volume_blank` 直测 | 四分支 | PASS |
| 10 | 配置 boot policy 全矩阵（7 输入组合） | 分类符合 §2 表 | PASS |
| 11 | **凭据链负例（rev 2 新增，review BLOCKER 1/AC4-a）** | 后端不可读 → BACKEND_UNAVAILABLE → `allow_admin_auth=false` 且 `allow_persist=false`；可读但未识别 → UNRECOGNIZED → 同样双拒绝；仅 PERSISTED/PENDING_INIT 允许认证 | PASS |
| 12 | **未授权首次写入负例（rev 2 新增，review BLOCKER 2/AC4-b）** | proven-blank 证据 → PENDING_INIT → `allow_persist=false`（自动首启写入必须不发生）；bootstrap 认证保持可用以触达显式初始化入口 | PASS |
| 13 | NVS 空白首挂（真实 NVS 库） | init 成功 ready=true；erase=0；读写改删回环 | PASS |
| 14 | NVS 读失败注入后重新 init | init 失败、ready=false；erase/prog=0；逐字节不变；read/write/delete/clear 全 `-EACCES` | PASS |

汇总：**130 checks, 0 failures**（rev 1 为 116；+14 来自新增链路负例与格式化失败
强化断言，policy 期望值按 rev 2 契约更新：PENDING_INIT 拒绝自动持久化）。既有
正常路径回归（#4、#13 等）保持通过。

### 无法 host 化、仅由目标构建 + 静态分析覆盖的部分
- `storage.c` 接线（RTOS mutex、XSPI 回调）：改动为"删除擦除/重启调用 + 换用已测
  probe/format 函数"；全仓 `lfs_format` 仅存于 `storage_lfs_format_volume`。
- `json_config_nvs.c` / `json_config_mgr.c` / `auth_mgr.c` 门控接线：决策表已注入
  测试；接线靠编译证据 + 代码审查。关键锚点：auth 三条消费路径全部汇聚于
  `auth_mgr_verify_password`（web_server.c:1075、api_auth_module.c:44、
  api_file_module.c:285）；core_init 阶段顺序 config(stage2) → security(stage6)
  保证 `credentials_trusted` 读取时已就绪。

## 5. 目标构建证据（AC4）

命令：`make app`（worktree，arm-none-eabi-gcc 15.2.Rel1）。
结果：**编译、链接通过，0 warning**，产物 `build/ne301_App.elf/.bin/.hex`。

| Region | rev 2 | rev 1 | 基线（改动前） |
|---|---|---|---|
| AXISRAM1_2_S | 3,846,756 B (91.74%) | 3,844,484 B (91.68%) | 3,843,052 B (91.65%) |
| SRAM_POOL | 902,944 B (50.79%) | 902,944 B (50.79%) | 902,432 B (50.77%) |
| AXI_SRAM_UNCACHED | 189,728 B (59.39%) | 189,728 B (59.39%) | 189,728 B (59.39%) |
| PSRAM | 57,273,376 B (91.03%) | 57,273,376 B (91.03%) | 57,273,376 B (91.03%) |

rev 2 增量（vs rev 1 +2,272 B AXISRAM）来自 auth 门控字段/分支、confirm 解析与
PENDING_INIT 逻辑；text 3,436,600 / data 410,132 / bss 58,366,040。

## 6. 真机遗留清单（本任务**未验证**，需要单独设备授权）

1. 真机 LittleFS 固件写路径掉电撕裂行为（#27 §2.4 开放项）。
2. 真机人为损坏 NVS/LittleFS 后的启动日志、`storage_get_lfs_state()` /
   `json_config_mgr_persist_blocked()` / `json_config_mgr_credentials_trusted()`
   的实际表现，以及管理登录被拒、敏感 API 401 的端到端行为。
3. 真机 96MB 卷空白探针耗时（仅 mount 失败路径执行；预计几十毫秒级，未实测）。
4. NEEDS_INIT 状态下显式格式化（现需 `{"confirm":"FORMAT"}`）的端到端流程。
5. **PENDING_INIT 设备的实际引导体验**：默认凭据登录 → 出厂复位初始化的完整
   流程（Web UI 需同步支持 `confirm` 字段与 pending-init 提示——UI 不在本任务
   scope）；量产是否改用烧站预初始化属 A/User 决策。
6. 降级会话的 Web 交互体验（当前仅日志 + 拒绝写入 + 拒绝认证，无 UI 提示）。
7. 显式格式化时卷上仍有打开文件句柄的边界（`storage_format()` 先 unmount 再
   format，比旧实现"挂载状态下就地 format"更安全；外部已打开句柄在 remount 后
   悬置属固有边界，需上层保证格式化前无活跃写入方）。

## 7. 与 #27 / #36 / #30 的衔接

- **#27**：关闭 `app-install-feasibility.md` 行 174 的 "mount 失败 → 静默格式化"
  缺口；行 266 固件写路径掉电项仍开放（§6.1）。App 安装事务（#30）可依赖
  "安装介质不会被启动路径格式化"。
- **#36**：迁移前提"固件不会擦空 NVS"满足；本任务在存储降级时的认证拒绝是最小
  fail-closed，不构成 #36 的认证安全方案；PENDING_INIT/降级状态的认证与 UX 体验
  应在 #36 统一设计。
- **#30**：不处理安装事务原子性；仅保证存储启动语义不再是事务破坏源。

## 8. 范围与边界重申

- 未修改：`FSBL/**`、`Frontend/**`、`Model/**`、`WakeCore/**`、Web 前端（格式化
  confirm 字段为 API 侧行为变更，前端适配遗留 §6.5）、Flash 分区表。
- 未做：真机刷写/格式化/擦除/复位/掉电实验；`ght` 写板；push/PR。
- 生产出厂初始化流程变更不在本 Issue 实施许可内，见 §2 末段。
