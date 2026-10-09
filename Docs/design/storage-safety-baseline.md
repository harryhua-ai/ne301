# 存储安全基线：LittleFS / NVS 非破坏性启动与故障恢复（Issue #37）

状态：已实现 rev 3（实验分支 `agent/37/ef9ccc5f`，基线 `c97ee19b`；rev 3 按第二轮
review 的 3 项 BLOCKER + 1 项验证缺口修订，并撤回 rev 2 中的 PENDING_INIT
bootstrap 设计）
验证边界：**仅 host 错误注入回归 + 目标固件编译**。零真机操作。

---

## 1. 背景与问题

#27 调查（`Docs/design/app-install-feasibility.md` §1.2/§2.1，行 174）记录的两个存储
风险（LittleFS mount 失败静默格式化、NVS 初始化失败擦除重启）加上配置层的默认
回写与假报成功问题，共同构成一条链：**存储异常不得静默擦除状态不明的用户数据/
配置，也不得把降级状态误报为可用或可信**。

rev 2/3 追加的两个尖锐后果：
- 配置 NVS 损坏/不可读时，RAM 里的**编译期默认密码不得成为有效管理员凭据**
  （rev 2 修 backend 不可信；rev 3 补"magic 合法但凭据 key 读失败"缺口）。
- proven-blank（介质证明为空）**不是权限**：无独立授权机制时，空白设备不做自动
  写入，公开默认凭据也不得提权（rev 3 撤回 rev 2 的 bootstrap 设计）。

## 2. 三态不变量（A 决定）与实现映射（rev 3）

| 不变量 | 实现位置 | 行为 |
|---|---|---|
| ① 已可信挂载/读取的旧卷与配置 → 原功能继续 | mount 成功即 `OK` 不写盘；magic 有效 → `PERSISTED`，per-key 新键回填与**可证明的 legacy 凭据迁移**（legacy key 真实读出 → 写新 key）保持原行为；凭据用 NVS 存的真实密码 | 正常路径零行为变化（`test_healthy_volume_mounts_and_keeps_files`、`test_legacy_credential_proven_migration`、`test_nvs_blank_init_writes_no_erase_and_roundtrips`） |
| ② 可独立确认的首次空白介质 → 仅经明确受权的初始化路径写入 | LittleFS：只读空白证明 → `NEEDS_INIT`，唯一写入路径 = 显式 `storage_format()`（需知情确认）；NVS_USER：全分区只读擦除校验证明空白 → `PENDING_INIT`——**无任何自动/过早 NVS 写、不发生公开默认提权、出厂复位入口同样拒绝**。现有已批准产品流程中不存在可证明的独立授权首启操作，因此空白设备保持待初始化；**首启 UX/安全策略属 A/User 决策**（rev 2 的"出厂复位作为受权首启入口"设计已按二轮 review 撤回） | "空白"须完整读回证明；mount 失败/magic 缺失不构成证明；**空白证明也不是写入授权，更不是权限** |
| ③ 状态不明/读取失败/损坏 → 保留介质 + 报告不可用，fail-closed | LittleFS：`UNAVAILABLE`；NVS init 失败：分区保留、not-ready、访问器 `-EACCES`；配置层：`UNRECOGNIZED`/`BACKEND_UNAVAILABLE`/`PENDING_INIT` → `persist_blocked` 拒绝一切持久化，且 `credentials_trusted=false` 拒绝管理认证；**PERSISTED 卷上凭据 key 双双读失败同样判"未知"→ 保留既有字节、不回写默认值、拒绝特权认证**（rev 3 BLOCKER 1） | 注入测试断言拒绝路径 erase/prog=0 且介质逐字节不变；真实 NVS 错误码链路负例（§4 #15-18） |

## 3. 改动清单（按层，rev 3 增量加粗）

### 3.1 `Custom/Hal/storage_safety.{c,h}`
- `storage_lfs_probe_and_mount()`：mount + 只读分类（OK / NEEDS_INIT / UNAVAILABLE）。
- `storage_lfs_volume_blank()`：全卷 0xFF 校验；读失败绝不误报空白。
- `storage_lfs_format_volume()`：唯一格式化路径，返回真实结果。**失败语义如实**：
  可能已部分擦除，卷保持 unmounted，状态/完整性**未被证明**。
- `storage_blank_check_range()`：可注入 read 的原始区域擦除校验（NVS 用）。

### 3.2 `Custom/Hal/storage.{c,h}`
- `lfs_mem_init`：删除静默格式化；probe + 状态 + 日志。
- `storage_init`：删除 NVS 失败擦除重启；分区保留、not-ready、访问器 fail-closed。
- `storage_format()`：`int` 真实结果；失败日志"state/integrity NOT verified
  (partial erase possible)"。
- 门面查询：`storage_get_lfs_state()`、`storage_nvs_ready()`、`storage_nvs_blank_check()`。

### 3.3 `Custom/Core/System/json_config_boot_policy.{c,h}`（纯逻辑、host 可测）
- `json_config_boot_classify()`：证据 → `PERSISTED / PENDING_INIT /
  UNRECOGNIZED / BACKEND_UNAVAILABLE`。
- `json_config_boot_allow_persist()`：仅 PERSISTED。
- **`json_config_boot_allow_admin_auth()`（rev 3 收紧）**：仅 PERSISTED。
  PENDING_INIT 拒绝——blank evidence is not permission，撤回 rev 2 的 bootstrap。
- **`json_config_boot_allow_factory_reset(persist_blocked)`（rev 3 新增）**：
  仅非 blocked 会话允许；reset 不是空白介质初始化路径，也不存在预开门闩。
- **`json_config_boot_assess_credential(auth_key_err, legacy_key_err)`（rev 3 新增）**：
  至少一个 key 真实读出 → `CRED_PROVEN`；两者皆失败（缺失或后端错误）→
  `CRED_UNKNOWN`（fail-closed）。

### 3.4 `Custom/Core/System/json_config_nvs.c`
- `json_config_load_from_nvs`：采证分类；`PENDING_INIT`/`UNRECOGNIZED`/
  `BACKEND_UNAVAILABLE` 零写入返回（`NOT_INITIALIZED`/`CORRUPTED`/`UNAVAILABLE`）。
- **凭据段改造（rev 3 BLOCKER 1）**：先读 `NVS_KEY_AUTH_PASSWORD`，失败再读
  legacy `NVS_KEY_DEVICE_INFO_PASSWORD`，交 `assess_credential`：
  - `PROVEN`（新 key 读出，或 legacy key 真实读出）：会话 `credentials_trusted=true`；
    legacy 命中时保留原有的一次性新 key 迁移写。
  - `UNKNOWN`（两者皆读失败）：**保留既有字节、不把编译期默认密码写回**，
    会话 `credentials_trusted=false`（auth 层随后拒绝），LOG_ERROR 明示。
    仅在启动装载阶段写会话标志（initialized 后的辅助装载不翻转）。
- **`json_config_save_auth_mgr_config_to_nvs` 门控（rev 3）**：凭据未证明的会话
  跳过密码 key 持久化（超时等非敏感字段照常），防止全量保存把默认密码写进
  未知状态的卷。
- fail-closed 持久化门：`save_to_nvs` choke + 7 个 per-key 写助手 + ISP 保存门。

### 3.5 `Custom/Core/System/json_config_mgr.{c,h}` + `json_config_internal.h`
- `json_config_mgr_init`：装载失败 → RAM 默认值、不写 NVS、`persist_blocked`、
  **`credentials_trusted=false`（含 PENDING_INIT，rev 3）**；返回 AICAM_OK 继续启动。
  装载成功分支**尊重 load 的凭据裁定**（不再是"成功即 trusted"）。
- `json_config_get_device_password()`：不可信时拒绝交付。
- **`json_config_set_device_password()`（rev 3）**：凭据未证明的会话拒绝改密。
- **`json_config_reset_to_default()`（rev 3）**：撤回 PENDING_INIT 解锁设计——
  一切 `persist_blocked` 会话（含 PENDING_INIT）拒绝出厂复位；仅健康 PERSISTED
  会话可复位。**BLOCKER 3 随之结构性消除**：不存在"先解锁后初始化"模式，
  门闩在所有失败/等待路径恒闭，无未知持久状态下的假成功。
- 诊断 API：`persist_blocked / credentials_trusted / degraded_reason`。

### 3.6 `Custom/Core/Security/auth_mgr.c`
- 凭据源不可信（含 PENDING_INIT）时：不物化默认密码哈希、不打印密码材料、
  `auth_mgr_verify_password()` 无条件拒绝（覆盖 web_server 敏感 API Basic 认证、
  登录端点、文件 API 三个消费方）、`auth_mgr_change_password()` 拒绝。
  #36 全面方案不在本任务。

### 3.7 `Custom/Services/Web/api/api_device_module.c`（Web 层唯二触点）
- `POST /device/storage/format`：需 `{"confirm":"FORMAT"}` 知情确认（缺失即拒，
  消息写明目标与后果；前端未适配为已声明 breaking change，UI 决策属 A/User，
  Frontend/ 未动）；失败响应**只陈述事实**（rev 3：移除"可重试"措辞——卷内容
  状态未知时不得鼓励再次破坏性操作）；成功才触发 captures 重建。认证强化属 #36。

### 3.8 显式入口保留清单
- `storage_format()` / format API：保留，知情确认 + 真实结果。
- `storage_nvs_clear()`：保留（无调用方）。
- 出厂复位：健康会话行为不变；blocked 会话（含 PENDING_INIT）拒绝。

## 4. 错误注入回归（AC4，host 端）

位置：`tests/storage_safety/`。链接固件同源真实代码：littlefs、NVS 库、
`storage_safety.c`、`json_config_boot_policy.c`，故障注入 RAM flash。

| # | 场景（注入） | 断言 | 结果 |
|---|---|---|---|
| 1 | 空白卷 mount 失败 | NEEDS_INIT；erase/prog=0；逐字节不变 | PASS |
| 2 | 损坏卷（非空白） | UNAVAILABLE；erase/prog=0；逐字节不变 | PASS |
| 3 | 读失败 | UNAVAILABLE——不可读永不判空白；erase/prog=0 | PASS |
| 4 | 健康卷 + 已有文件 | OK；文件原样可读 | PASS |
| 5 | NEEDS_INIT 卷显式格式化 | 返回 0；可挂载可写 | PASS |
| 6 | 健康卷显式格式化 | 返回 0；旧文件确实消失 | PASS |
| 7 | 格式化写失败注入 | 非 0；unmounted；erase>0 且介质被修改（部分擦除真实存在） | PASS |
| 8 | `storage_blank_check_range` 矩阵 | 五分支 | PASS |
| 9 | `storage_lfs_volume_blank` 直测 | 四分支 | PASS |
| 10 | boot policy 全矩阵（7 输入组合） | 分类符合 §2 表 | PASS |
| 11 | 凭据链负例（rev 2） | 不可读/未识别 → 认证+持久化双拒绝；仅 PERSISTED 允许认证（rev 3 收紧） | PASS |
| 12 | 未授权首次写入负例（rev 3 强化） | proven-blank → PENDING_INIT → persist=false、**auth=false**、**factory_reset=true 的 blocked 门=false** | PASS |
| 13 | NVS 空白首挂（真实 NVS 库） | init 成功；erase=0；读写改删回环 | PASS |
| 14 | NVS 读失败注入后重新 init | init 失败 ready=false；erase/prog=0；逐字节不变；访问器 `-EACCES` | PASS |
| 15 | **真实链路 (i)a（rev 3）**：magic 合法卷上两个凭据 key 均缺失（真实 NVS `-ENOENT` → 固件同款错误映射 → 真实 `assess_credential`） | 判 `CRED_UNKNOWN`；卷内真实读回确认**没有**播种 `auth_password` key | PASS |
| 16 | **真实链路 (i)b（rev 3）**：同上但两个 key 读均为后端 IO 错误（故障注入） | `CRED_UNKNOWN` | PASS |
| 17 | **真实链路 (iv)（rev 3）**：legacy key 真实存在并读出 | `CRED_PROVEN`；按固件语义执行迁移写后真实读回一致 | PASS |
| 18 | **真实链路 (ii)+(iii)（rev 3）**：真实空白 NVS + 公开默认凭据 | PENDING_INIT → `allow_admin_auth=false`、`allow_persist=false`、`allow_factory_reset(true)=false`（门闩恒闭，无预开） | PASS |

汇总：**167 checks, 0 failures**（rev 2 为 130；+37 来自凭据 provenance 矩阵、
factory-reset 门、四条真实 NVS→policy 链路测试）。正常旧数据/legacy 认证回归
（#4、#13、#17）保持通过。链路深度说明：决策函数与真实 NVS 错误码在 host 真实
链接；`json_config_nvs.c`/`auth_mgr.c` 的接线由目标构建 + 代码审查覆盖（锚点：
auth 三消费方汇聚 `auth_mgr_verify_password`；core_init config stage2 → security
stage6）。

## 5. 目标构建证据（AC4）

`make app`（arm-none-eabi-gcc 15.2.Rel1）：**通过，0 warning**。

| Region | rev 3 | rev 2 | 基线 |
|---|---|---|---|
| AXISRAM1_2_S | 3,847,124 B (91.74%) | 3,846,756 B (91.74%) | 3,843,052 B (91.65%) |
| SRAM_POOL | 902,944 B (50.79%) | 902,944 B (50.79%) | 902,432 B (50.77%) |
| AXI_SRAM_UNCACHED | 189,728 B | 189,728 B | 189,728 B |
| PSRAM | 57,273,376 B (91.03%) | 57,273,376 B (91.03%) | 57,273,376 B (91.03%) |

text 3,436,960 / data 410,132 / bss 58,366,040。

## 6. 真机遗留与 A/User 待决清单

1. 真机 LittleFS 固件写路径掉电撕裂行为（#27 §2.4 开放项）。
2. 真机损坏注入后的启动日志与三态/凭据门实际表现（登录拒绝、敏感 API 401）。
3. 真机 96MB 空白探针耗时（仅 mount 失败路径）。
4. format API `{"confirm":"FORMAT"}` 端到端（前端适配是独立 A/User 决策；
   Frontend/ 未动）。
5. **PENDING_INIT 设备的首次使用策略（A/User 决策，B 已撤回已方设计）**：当前
   实现下空白设备管理认证被拒、无自动初始化、出厂复位拒绝——最安全但完全不可
   通过 Web 引导；需要 A/User 定义独立授权首启机制或烧站流程后另行实现。
6. 降级会话的 Web 交互提示（当前仅日志 + 拒绝，无 UI）。
7. 格式化时已打开文件句柄的固有边界（需上层保证无活跃写入方）。

## 7. 与 #27 / #36 / #30 的衔接

- **#27**：关闭行 174 "mount 失败 → 静默格式化"缺口；行 266 仍开放。
- **#36**：迁移前提"固件不会擦空 NVS"满足；本任务的凭据 fail-closed 是 #37 存储
  保护，不是 #36 迁移特性；口令存储/会话/日志全面方案归 #36。
- **#30**：存储启动语义不再是安装事务的破坏源。

## 8. 范围与边界重申

- 未修改：`FSBL/**`、`Frontend/**`、`Model/**`、`WakeCore/**`、Web 产品 UI、
  Flash 分区表。
- 未做：真机操作；`ght` 写板；push/PR；amend。
