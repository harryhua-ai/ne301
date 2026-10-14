# App Host Control — #56 Host 受信热配置控制与业务状态只读桥接

Status: implemented on `agent/56/c73bca3c` (base `1cf39ed8`).
Authority: NE301 issue #56 ght-contract (AC1–AC4) + A READY decision 2026-10-13
+ A 控制命令与只读快照 Host 验收门 2026-10-12 + A 受信命令面 2026-10-12.
Wire/ABI authority: ne30x-app `docs/app-package-protocol-v3.md` §5.3–§9.6
(唯一 32B 通用头、非阻塞控制、回执、快照、命令状态与旧 ABI 互操作权威)。
App 侧业务 codec 事实: ne30x-app #21 (144B 业务载荷/176B 含头、981B 含头最坏
快照 — 离线测试配额，非设备承诺)。

This file carries all explanatory prose; per governance the new/changed
`.c/.h`/Makefile/test sources carry no new comments.

## 1. What is implemented

管理员可调用的**单一有副作用命令面**（CONFIG_APPLY / COUNTERS_RESET）与
**App 自有业务快照的只读投影**，面向当前受信 v3 独立 App 长会话：

- `Custom/Services/AppHost/app_host_control.h/.c` — 便携控制面核心（不依赖
  RTOS，互斥/时钟经 `app_control_backend_t` 注入）：
  - **32B 通用消息头**（`NFC3`/`header_version=1`，§6.3）的编码与 fail-closed
    解析；`msg_type` 1=CONFIG_APPLY、2=COUNTERS_RESET、3=回执、4=快照。
  - **会话世代**：Host boot 作用域自 1 起非零单调 u32，每次会话建立 +1，
    `0xFFFFFFFF` 用尽后拒绝新会话（禁回绕，§9.1）。
  - **命令登记（§9.2 顺序）**：高权限管理员经 Host API 提交；判定序为
    鉴权/bit6 → 终态缓存幂等（同 ID 同语义 `CACHED`/异语义立即
    `DUPLICATE_ID_CONFLICT`）→ 形状非法 `INVALID`（request_id=0、
    expected_config_generation=0、CONFIG_APPLY 空载荷、RESET 带载荷、
    未知 flags 位）→ 单一待决 `BUSY`/同 ID 异语义冲突 → 已知世代
    `CONFLICT` → RESET 未确认 `UNCONFIRMED` → 配额 `QUOTA_EXCEEDED`
    （含 32B 头，`min(签名 control_max, Host 512B RAM)`，拒绝不截断）→
    存储不可知纵深防御 `STORAGE_GUARD`（§13.8：会话最近结果为
    FAILED_STORAGE_UNKNOWN 时拒绝新的破坏性登记，由后继非 UNKNOWN 终态
    回执清除）。
  - **交付 `control_next`**：`PENDING → DELIVERED` 原子单命令；无命令合法
    `NO_EVENT=1`；`BUFFER_TOO_SMALL` 经 `*actual_len` 报完整需求长度且
    **不消费**命令；交付后不重发。
  - **回执 `control_reply`**：仅缓存同会话同 `request_id` 与已交付命令匹配的
    首个有效回执（世代匹配、非零 ID、`config_generation` 非零、
    `result_status ≤ 7`、`total_len==32` 无载荷）；首个有效回执定终态并缓存
    （同 ID 后继回执不改缓存结果）；`APPLIED` 前进 Host 已知
    `config_generation`；`REJECTED_*`/`FAILED_*`/其余结果不伪成功；所有
    有效回执的世代证据更新已知世代基线（§9.2.5 CONFLICT 依据）。
  - **快照 `snapshot_publish`**：仅当前会话 + bit7；`total_len` 一致且
    ≤ `min(签名 snapshot_max, Host 2048B RAM)` 超限整体 `QUOTA_EXCEEDED`；
    头内 `session_generation=request_id=0`、`flags=0`、
    `config_generation` 非零（App 自述世代不作为认证）；载荷必须为严格
    UTF-8 单个 JSON 对象——重复键（含转义等价键）任意深度拒绝、`NaN`/
    `Infinity` 拒绝、尾随数据拒绝、非法/过长 UTF-8 拒绝、深度 >16 拒绝
    （Host 资源上界）；校验通过后在回调返回前**同步复制**到有界 RAM 快照
    （无悬空指针），并从**当前可信回调上下文**绑定会话身份/世代/到达时间/
    每会话自 1 起单调的可见版本；Host 不解释业务键。
  - **新鲜度/失效**：距到达 >10s 标 `stale`；会话关闭/换会话/Host 重启
    （静态复位）清空 RAM 快照 → `unavailable`；`DELIVERED` 未回执即会话
    结束 → 可观察 `UNKNOWN` 记录（世代/ID/类型/计数）。
- `Custom/Services/AppHost/app_host_session.h/.c` — v3 运行时扩展：
  - **60B/13 回调 v3 API 表**（`control_next`/`control_reply`/
    `snapshot_publish` + 沿用 v2 语义的 10 函数），`table_size`/
    `abi_version=0x00030000`，静态断言钉定布局；v2 48B 表与 v1 run-once
    路径逐字节不变（零退化）。
  - **v3 会话启动**：container 3 经 v3 准入策略（`abi_table_size=60`、
    `supported_caps_mask = 0xF7`（log/tick/ai_events/app_state/lifecycle/
    control/snapshot；**不含** bit3 reports——#44 未合流，Host 不虚报能力，
    声明 bit3 的包按 §4.2 拒绝）、`control_capacity=512`、
    `snapshot_capacity=2048`、`state_quota_capacity=8192`、
    `v3_business_profile=GENERIC_FULL(2)`——新增的 Host 策略点，语义为
    "通用 v3 受控热命令会话"，不对 NONE/LC 两个既有 profile 的任何判定
    做修改）验证；控制配额取 `min(签名 control_max/snapshot_max, Host
    容量)`；会话开启即分配世代并打开控制面。
  - **#46 CAS 集成**：v3 声明 bit4 时以当前受信安装身份 +
    `state_quota` 绑定 `app_state_session`；`state_read`/`state_commit`
    直通 #46 CAS（原子/CRC/双槽标记语义不变）；未声明 bit4 或 v2 会话
    一律 `UNAUTHORIZED`。
  - **teardown**：停止/会话关闭/ai_attach 失败统一关闭控制面（待决清除、
    已交付未回执 → UNKNOWN 可观察记录、快照清空）并解绑状态存储。
- `Custom/Services/Web/api/api_app_control_module.h/.c` — 管理员 API
  （AC3，全部 `web_server_high_privilege_allowed` 先于 body 解析，注册于
  `NE301_APP_HOST_POC` 守卫）：
  - `POST /api/v1/app/control/config` — CONFIG_APPLY 登记
    （`request_id`/`expected_config_generation`/`payload_base64` 非空，
    Host 不解析不重造 App-owned opaque payload）。
  - `POST /api/v1/app/control/reset` — COUNTERS_RESET 登记
    （空载荷；`destructive_confirmed` 为 false/缺省时 Host 侧
    `UNCONFIRMED`，不替代 Web 显式确认义务）。
  - `GET /api/v1/app/control/status` — 命令生命周期查询（登记 ≠ 生效：
    `registered/cached/already_registered/refused` + 终态结果、已知世代、
    UNKNOWN 对账记录）。
  - `GET /api/v1/app/business/snapshot` — 只读快照投影
    （`availability: fresh|stale|unavailable` + Host 绑定的身份/世代/版本
    /`state_revision`/age；载荷以原样 JSON 嵌入，Host 不解析业务键）。
- `Custom/Services/Web/api/api_app_install_module.c` — 安装门增加 v3 策略
  分发（container 3 → 与会话一致的 v3 准入策略）；v1/v2 判定逐字节不变。
- `Custom/Services/AppHost/app_admission.h/.c` — 新增
  `APP_ADMISSION_V3_BUSINESS_GENERIC_FULL = 2` 策略点（仅放宽 PARAM 边界
  至新枚举值；NONE 与 LINE_CROSSING_64 的全部既有拒绝语义不变）；
  `app_admission_container_version` 增加 v3 magic 识别（#55 遗留的探测
  缺口，v1/v2 返回值不变）。
- `Appli/Makefile` / `Custom/Services/Web/web_service.c` — 新源文件与
  路由注册（`NE301_APP_HOST_POC` 守卫内）。
- `Docs/design/app-host-control.md` — 本文档。

## 2. Tests

`tests/app_host_control/`（宿主原生，`make test`）：

- `gen_control_vectors.py` → `control_vectors.h` — 独立生成的 v3 签名黄金包
  （d=1 测试标量，`test-publisher` 身份）：`0xF7` 全能力（无 bit3）、
  `0x63` 最小控制、bit8 未知能力、`control_max=1024` 超 Host 配额、无 bit4
  五个向量，正式 §2/§3 容器/manifest 编码。
- `test_app_host_control.c` — 320 checks：
  - 严格 JSON 边界（§8.3 正/负例族：重复键/转义等价重复键/NaN/Infinity/
    尾随/顶层数组/坏转义/孤立代理对/裸控制符/非法与过长 UTF-8/深度界）；
  - 登记判定序（§9.2 全部拒绝类 + 幂等/冲突/BUSY/CONFLICT/UNCONFIRMED/
    QUOTA/防御性 STORAGE_GUARD 及其清除）；
  - 交付与回执（wire 字节精确断言、BUFFER_TOO_SMALL 保留命令、错误世代/
    ID/载荷/结果码回执全部 `INVALID_ARGUMENT` 不改状态、首个有效回执定
    终态、重复回执不改缓存、APPLIED 前进已知世代而失败结果不冒充）；
  - 跨会话隔离（世代单调、世代禁回绕、旧世代回执拒绝、会话清除、
    DELIVERED→UNKNOWN 对账记录、快照 unavailable）；
  - 快照生命周期（同步拷贝无悬空指针、身份/世代 Host 绑定、可见版本单调、
    5s/10s 新鲜度门、quota 超限整体拒绝、App 自填世代/ID 拒绝）；
  - 真实 v3 会话夹具（安装→启动→App 侧 `control_next`/`control_reply`/
    `snapshot_publish`/`state_read`/`state_commit` 全链路 + #46 CAS 修订
    冲突保持）；
  - 旧能力不越权（v2 会话三新回调与 state 全部 `UNAUTHORIZED`、v3 未声明
    bit7/bit4 能力调用拒绝、未知能力位/超配额包安装拒绝、最小
    `control_max=32` 配额门）。

组合回归（同一工作树、真实合流基线 `1cf39ed8`，全部 0 failures）：
admission v1 179 / v2 84 / v3 61 / gate 97；installer 271 / 280 / 165 / 54；
app_host_run 214；app_host_session 413；app_state 188 / 260；
storage_gate 802 / 267 / 1960 / 83；app_host 21；新增 app_host_control 320。

## 3. Deliberate B-minimal choices (A 评审关注点)

1. **`config_generation` 基线来源**：Host 已知世代仅由回执（`APPLIED` 新世代
   及其余结果的当前活跃世代）与快照头 `config_generation` 更新；无基线时
   允许登记（App 侧世代检查为权威防线，§13.7）。
2. **`DUPLICATE_ID_CONFLICT` 也覆盖在途命令**：同 ID 异语义在 PENDING/
   DELIVERED 期间立即冲突（比 §9.2.4 的 `BUSY` 更早、更 fail-closed）；
   同 ID 同语义在途为 `ALREADY`（不重排副作用）。
3. **存储未知防御的语义边界**：Host 只依据线级证据（最近终态回执为
   FAILED_STORAGE_UNKNOWN）拒绝破坏性登记；快照 JSON 内的业务故障字段
   （如 `storage.fault`）按 §8.1 所有权边界不解释。
4. **reports (bit3) 不提供**：#44 未合流，v3 会话/安装策略的
   `supported_caps_mask` 不含 bit3 且 `report_capacity=0`；声明 bit3 的 v3
   包在准入被拒（§4.2/M10），不做"准入但调用 UNAUTHORIZED"的虚报。
5. **GENERIC_FULL profile**：新增 Host 策略枚举值 2（不修改 NONE/LC 判定），
   表达"通用 v3 受控热命令会话"的面（bits 0/1/2/4/5/6/7，无业务专属强
   约束）；真实 line-crossing-64 包需 bit3，待 #44 合流后由策略演进接纳。
6. **JSON 深度/键数上界**（16 层/48 键）与快照 RAM 上界（2048B）、控制
   RAM 上界（512B）为 Host 设备事实（§13.10/§13.13 最小编码选择），超限
   fail-closed。
7. **重复回执容错**：终态后同 ID 回执返回 OK 且不改缓存（§7.1"不改变已
   缓存结果"的字面实现）；不匹配任何缓存/在途命令的回执为
   `INVALID_ARGUMENT`。

## 4. NOT RUN / 边界（如实标注）

- **真机热配置 NOT_RUN**：无板卡授权；模拟宿主验证仅支持软件工程判断，
  不宣称真机 PASS。
- 设备 NVS/LittleFS 写入、真实外网、生产 OTA：NOT_RUN（无授权）。
- App #21 真实 Line Crossing 业务候选 schema 的端到端消费：本 Issue 仅以
  opaque 有界载荷验证控制面（协议 v3 §14.6 边界一致）。
- #57 Web UI：未实现（non_goals）。
- #44 报告上行：未触碰其文件；bit3 维持不提供（无假阻断）。
