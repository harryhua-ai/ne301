# NE301 存储启动非破坏性安全设计（Issue #37，REPLAN Contract 2026-10-10，A 评审修正轮）

- 基线：`002e963a4aa07fd7f9690087bec09404df301304`
- 本轮（delta）修正 A 对 Candidate `92c7cdba` 的 5 项阻塞（B1–B5）。
- 第二轮增量（Candidate `43c4d091` → 本轮）修正 A 第二轮 R2-B1/R2-B2/R2-B3 与
  自审补充 S1/S2，详见 §11。
- 本文档是解释性设计文档；按 User 要求，所有新增/修改代码零注释。

## 1. 目标与五态模型（B1 修正）

Contract 要求：LittleFS/NVS 启动与异常恢复非破坏；默认初始化仅发生在"整片 NVS_USER
完整可靠空白检查且零 I/O 错误"的可信空白介质上；未知/故障态保留原始字节、拒绝写入
且可观测；健康态正常 GC/保存/旧数据迁移不受影响。

每个受保护介质区域（NVS_FACTORY、NVS_USER、LittleFS 卷）在启动时被分类：

| 状态 | 判定条件 | 公共写/删/清（storage_nvs_write/delete/clear） | 底层范围写/擦（storage_flash_write/erase） |
|------|----------|------------------------------------------------|--------------------------------------------|
| `UNTRUSTED` | 空白探测读取错误，或 NVS init 失败（-EDEADLK/-ESPIPE/-EIO 等） | 拒绝 | 拒绝 |
| `STARTUP` | 空白探测成功、可信启动扫描进行中 | 拒绝 | 允许（仅该区域：健康旧卷启动内部合法 GC/恢复擦除需要） |
| `BLANK` | 整片扫描全为擦除值(0xFF) 零读错，且 NVS startup 干净完成 | 允许 | 允许 |
| `HEALTHY` | 非空白、零读错、NVS startup 干净完成（无撕裂/延迟恢复证据） | 允许 | 允许 |
| `DEGRADED` | startup 完成（rc=0）但 `startup_flags` 含 `NVS_STARTUP_TEAR_SEEN`，或启动恢复擦除的可证明性检查未通过而置 `NVS_STARTUP_RECOVERY_DEFERRED`（§11.1） | 拒绝 | 拒绝（该区域） |

B1 修正点（相对上一 Candidate）：
- 探测成功后、`nvs_init` 完成前，区域处于 `STARTUP` 而非直接 HEALTHY/BLANK：
  公共破坏性写入从"启动当次"起就被门拒绝，HEALTHY/BLANK 只在可信启动检查
  干净完成后授予（"证实确实健康才开放破坏性写入"）。
- 健康旧卷启动内部合法 GC/擦除不受影响：STARTUP 态下该区域的底层范围写/擦
  保持可用（`storage_media_gate_range_writable` 对 STARTUP 返回允许），仅公共
  API 写入面被关闭。
- 撕裂/延迟恢复证据（TEAR_SEEN / RECOVERY_DEFERRED，含"下一扇区非擦除值但
  因撕裂证据跳过恢复擦除"的场景）在 startup rc=0 完成后把会话降级为
  `DEGRADED`：写、删、清、GC（公共面与该区域底层范围面）全部拒绝且带日志，
  读取不受影响。再次重启重新分类；原始字节全程保留。
- 状态机实现收敛在新模块 `Custom/Hal/storage_nvs_session.c`（`storage_nvs_session_init`，
  纯逻辑 + flash ops/mutex 注入，可在主机链接）：`nvs_blank_check`（读错 →
  UNTRUSTED）→ 置 STARTUP → `nvs_init`（失败 → UNTRUSTED；成功且含撕裂/延迟
  证据 → DEGRADED；干净 → BLANK/HEALTHY）。`storage.c::storage_nvs_init` 仅注入
  真实 flash ops 与 osMutex 后转发。

"全空白"只能由对整片区域的原始字节扫描证明；magic 缺失、仅出厂身份键、
旧 schema 数据、部分写入、损坏一律不是空白证据。无法可靠分类 = UNTRUSTED。

## 2. 介质安全门（session gate）

`Custom/Hal/storage_media_gate.c/.h`（纯 C，无 OS 依赖，主机可编译）：

- 每分区保存状态 + 状态的按位取反影子副本；查询不匹配视为 UNTRUSTED
  （防 CLI `mem w` 单标志翻转；双影子蓄意篡改属软件门不可证明范围，见 §8）。
- 区域注册表：`storage_flash_write / storage_flash_erase / storage_flash_erase4K`
  在直写路径上检查目标范围与 UNTRUSTED / DEGRADED 分区重叠，重叠即拒绝；
  STARTUP 态区域放行底层范围操作（启动内部恢复 GC 专用），非重叠区域不受影响。
- `storage_nvs_write / storage_nvs_delete / storage_nvs_clear` 统一先查门：
  `writable()` 仅 BLANK/HEALTHY 为真。这一个收口点覆盖 CLI `fset`/`factory` 族、
  固件后台写者、json_config 逐键与全量回写、`factory_config_write`。

## 3. LittleFS 启动（AC1）

`storage.c::lfs_mem_init`：mount 失败不再自动 `lfs_format`。对整卷只读空白探测：
探测读取错误或卷非空白 → 拒绝格式化，保持 unmounted，文件操作沿既有
`storage_lfs_isready` 路径失败；卷完整空白且零读错 → 格式化并挂载。

`storage_format()` 返回真实 int（0 成功 / 负失败）。

Web `POST /api/v1/device/storage/format`（B3 修正）：
- **恢复既有请求形状**：不再要求 `{"confirm":"FORMAT"}`。既有受权前端
  （`Web/src/pages/storageManagement` 先管理员密码验证后调用
  `formatFlash()`，POST 不带额外字段）恢复可用。是否引入显式知情确认契约
  属产品/兼容性决策，按 A 要求不在本任务擅改，作为 escalation finding 交 A。
- `storage_format()` 失败时返回 **真实失败响应**（`api_response_error`，
  HTTP 500 + 失败原因与结果码），不再构造 `success:false` 子字段伪装成功；
  现有 Web 客户端可据此真实判定失败。成功路径维持 `{"success":1}` +
  captures 目录重建。

## 4. NVS 启动（AC2，B1）

`storage_init` 每分区调用 `storage_nvs_session_init`（见 §1）。启动继续、
分区保留、拒绝可观测；不再整分区擦除 + 复位。

## 5. 配置加载与凭据 fail-closed（AC2/AC3，B2 修正）

`json_config_load_from_nvs`（返回值语义全面收紧，上一轮恒返回 AICAM_OK）：

- NVS_USER 为 UNTRUSTED/STARTUP → 立即失败返回 AICAM_ERROR：零读取、零写入、
  不服务默认值、原始字节保留（fail-closed 一步到位，也消除故障介质的读噪声）。
- BLANK/HEALTHY/DEGRADED → 正常逐键读取；DEGRADED 下缺键回填/迁移写入会被
  门拒绝并聚合进失败（见 §6），读取本身仍填充 RAM。
- 全部读路径（主路径 `result = json_config_nvs_read_*` 约 200 处）在读取返回
  未知错误（非 OK 且非 NOT_FOUND）时聚合失败：读错不再"静默用默认值冒充健康"。
- 缺键默认回写、config_version 前向迁移、magic 首启写入、RTMP/RTSP/timer 等
  迁移写、REPORT_CONTENT 归一化写、invariants 修正写、首启全量保存
  `json_config_save_to_nvs`：失败全部聚合（`json_config_agg_*` 助手，首失败
  保留语义），任一失败 → load 失败（可观测，不虚报已完整持久化）。
- ISP 大块（contrast LUT / CCM / AWB / lux）读取：-ENOENT 容忍（旧配置无此键），
  其它负值聚合失败。ISP 子参数的 `if (read == AICAM_OK)` 条件式读取维持既有
  容忍（可选调优键）；总线级故障会先被同函数前面的强制键读取（如 isp_valid）
  聚合捕获。
- **凭据信任判定**（新纯函数 `json_config_boot_gate_credential_trusted`）：
  会话凭据仅在以下情形可信——
  1. `auth_password` 读取成功（真实存储凭据）；
  2. `auth_password` 可证明缺失且旧键 `dev_info_password` 读取成功（合法迁移，
     RAM 持有真实旧凭据）；
  3. 两键均可证明缺失且介质已证明全空白（真正的首次默认初始化，REPLAN 保留）。
  其余（UNKNOWN 读错、两键缺失但介质非空白等）→ 会话凭据不可信 → load 失败。
  读错误时 RAM 凭据缓冲清零（不残留栈垃圾、不误当默认值）。
- **凭据持久化成功判定**（R2-B2 修正，`json_config_boot_gate_credential_trusted_persisted`）：
  上述第 3 类（全空白首启默认凭据）仅在**本次默认凭据 NVS 写入实际返回
  AICAM_OK** 时才可信；写入失败（含介质级写故障）→ 会话凭据不可信 → load
  失败 → 默认口令登录与改密在同一会话被拒，原始 NVS 保留，二次重启重新
  分类（此时介质已非空白、两键仍缺失 → 继续不可信）。第 1/2 类的信任锚点
  是闪存中已证明存在的凭据本身，迁移/回填写失败不回退该信任（迁移失败本身
  仍经 agg 聚合可见，见 §6）。无故障的全空白首次默认登录与改密保持不变。
- 凭据读取成功/可迁移时（含 DEGRADED 介质），Web 登录仍对真实存储凭据工作；
  写被门拒绝、change password 返回真实失败。任何场景下都不会对"RAM 公开默认
  值"认证成功。

`json_config_mgr_init`（fail-closed + 可用性兼顾）：
- load 失败 → `config_healthy = FALSE` 并打错误日志；**不再** memcpy 默认配置
  全量回写 NVS（该旧回退路径会把默认值写进故障/读错介质的键上，已删除）。
- 管理器仍标记 initialized（保持 core_init 阶段链与设备可用性，boot 不中断），
  但凭据信任独立随动（`json_config_nvs_session_credential_trusted`）。
- 新增查询接口：`json_config_mgr_config_healthy()`、
  `json_config_mgr_credential_trusted()`。
- `json_config_get_device_password`：`credential_trusted != TRUE` → 返回错误
  （登录无法对 RAM 默认值成功）。
- `json_config_set_device_password`：凭据不可信 → 拒绝；凭据可信时先持久化
  后更新 RAM（持久化失败 RAM 不变、返回真实失败）。
- `json_config_save_to_file` / `json_config_mgr_deinit`：会话不健康（此前存在
  读错/写失败）→ 拒绝全量保存（防止把"读错键的 RAM 默认值"覆盖存储真值）；
  deinit 打印抑制日志。

`auth_mgr.c`（Web 登录/改密的实际裁决点，B2 必修面）：
- `auth_mgr_init`：`json_config_get_device_password` 失败时**不再回退编译期
  公开默认口令**；清零凭据、置 `credential_trusted = FALSE`、打错误日志。
- `auth_mgr_verify_password`：未初始化或凭据不可信 → 恒 FALSE（fail-closed）。
- `auth_mgr_change_password`：凭据不可信 → 拒绝；且改为先经
  `json_config_set_device_password` 持久化，持久化失败返回真实错误
  （旧实现改内存后忽略持久化失败并报告成功，已修）。
- 保留行为：真实首次空白启动默认凭据登录/改密、健康设备真实凭据登录/改密
  完全不变（E2E 回归覆盖）。

## 6. 写失败聚合（AC2/AC4，B4 修正）

- 根因修复：`json_config_nvs.c` 内所有保存/加载路径的逐键写统一经
  `json_config_agg_{string,uint8,uint32,uint64,int32,float,bool,blob}` 助手
  （首失败保留语义：`agg` 记录第一个失败，后续成功不覆盖）。上一轮
  "前键失败 + 末键成功 → 函数返回 AICAM_OK，顶层 aggregator 看不见"的
  覆盖问题在源头消除。
- 专项修复：`json_config_save_poe_config_to_nvs` 恒返回 AICAM_OK（已改为
  返回聚合值）；`json_config_save_network_service_config_to_nvs` 对 PoE
  子结果只记日志不合并（已合并）；`json_config_save_mqtt_service_config_to_nvs`
  对 base 子结果同样（已合并）；`json_config_set_video_stream_mode` 同模式
  （已合并）；ISP 保存的数十处裸写（含 `storage_nvs_write` 直写大块）全部
  纳入聚合。
- 健康设备合法保存不受影响：全部键写入成功时聚合值 == OK，行为与旧版一致
  （E2E 健康回归覆盖：同调用先故障返回失败、无故障返回成功）。

## 7. 验证（AC4，B5 修正）

`tests/storage_gate/`，两个主机测试二进制，全部在隔离环境运行（无真机操作）：

`test_storage_gate`（单元/组件级，751 checks，0 failures）——链接真实
`nvs.c`、真实 `littlefs`、真实 `storage_media_gate.c`、真实
`json_config_boot_gate.c` 与新增 `storage_nvs_session.c`，对 RAM 伪闪存注入
故障。新增用例：STARTUP/DEGRADED 门矩阵；`storage_nvs_session_init` 撕裂 →
DEGRADED → 拒写、二次重启字节保全；干净恢复 → HEALTHY 且启动 GC 合法执行；
空白卷 → BLANK 授权首启写入；探测读错/`nvs_init` 失败 → UNTRUSTED 字节保全；
credential_trusted 策略矩阵。

`test_storage_e2e`（端到端链路级，251 checks，0 failures）——**真实链接并执行**
`storage_nvs_session_init`（storage_nvs_init 的完整决策体）→
`storage_media_gate` → 存储层后端（双 NVS 分区 + 真实 littlefs 的
`storage_format`）→ **真实 `json_config_nvs.c`**（load/save 全量）→
**真实 `json_config_mgr.c`**（`json_config_mgr_init`/deinit/password）→
**真实 `auth_mgr.c`**（init/verify/login/change_password）→
**真实 `api_device_module.c`**（`device_storage_format_handler`，含真实 cJSON
响应对象）。未在链上的外部符号由 `gen_stubs.py` 从对象表自动生成空桩（仅链接
期需要，不在被测路径上）。覆盖故障族：

1. 可信全空白首启：默认初始化、默认凭据登录、改密持久化、二次重启读回
   （既有流程保持）；
2. UNTRUSTED（空白探测读错）：写/删/清拒绝、load 失败、config 不健康、
   get/set password 失败、auth fail-closed（对编译期公开默认口令登录失败）、
   原始字节逐字节不变、恢复后真实凭据可用；
3. DEGRADED（构造撕裂卷，凭据可读）：写/删/清拒绝、对真实存储凭据登录可用、
   对公开默认口令登录失败、改密真实失败、二次重启字节保全；无凭据变体
   （auth ATE 损坏）→ 对默认口令与存储口令登录均失败（fail-closed）；
4. B4 首写失败族：健康卷删三键后注入"仅首写失败、后续写成功"→ load 返回
   失败（上一 Candidate 此处返回 OK）；同函数无故障重试成功；全量保存
   同族（`json_config_save_to_file` 先失败后成功）；
5. B4 密码迁移写失败族：legacy 卷迁移写失败 → load 失败可见；RAM 仍持真实
   legacy 凭据（登录对 legacy 有效、对默认无效）；持久化失败的改密返回
   真实错误且 RAM/存储均不变；无故障重启后迁移持久化成功；
6. B3 格式化 handler：GET → 405 真错误；POST（既有前端形状，无额外字段）+
   格式化成功 → 成功响应 + reload 调用；注入格式化失败 → `api_response_error`
   真失败（非伪装 success）、reload 不调用。

限制声明：主机模拟链接了除 flash 驱动层以外的全部决策与持久化代码；
XSPI/NOR 驱动、ThreadX/CMSIS 与 Web 分发器为 shim。主机 PASS 不冒充真机
PASS；真机断电/撕裂时序未测试（无硬件操作授权）。

### 真实目标读错误传播（B5 后半：fail-closed 说明）

真实目标 `storage_flash_read` 从 XSPI memory-mapped 窗口 `memcpy` 后固定返回 0，
无法像 fake_flash 那样返回可验证的读错误。结论与边界：

- NVS 空白探测路径的"探测读错误 → UNTRUSTED"分支在真机上结构性不可达：
  memory-mapped 读不会以错误码形式失败，总线异常表现为 HardFault → 看门狗
  复位 → FSBL 重启路径。该故障注入在主机被证明正确（fail-closed），真机上
  该分支属于防御纵深而非可达路径。
- 真机可达的故障面由其余分支覆盖：非空白/部分写入/损坏/撕裂 → probe 结果 +
  可信 startup 扫描（`nvs_flash_*` 对 `XSPI_NOR_Read` 命令模式返回码敏感的
  路径，如 littlefs 的 `mem_block_read`）→ UNTRUSTED/DEGRADED/拒绝格式化。
- 改进选项（未实施，记录供决策）：把 `storage_flash_read` 改为 XSPI 命令模式
  读取以获取真实返回码，代价是 NVS 路径读性能与 memory-mapped 并发语义
  （其它子系统依赖 memcpy 语义）；或读取后对关键元数据区做 CRC 校验。二者
  都需要真机测量，超出本任务授权，故保持现状并在文档声明。
- 无硬件不可验证项：真实 NOR 位翻转下的 CRC 命中率、断电撕裂时序、
  HardFault-复位路径的端到端行为。

## 8. 残余风险与边界

- `mem w` 双写篡改门影子：软件门不可证明防住任意内存写；单标志翻转不可用。
- 凭据会话（登录后的 token/session 生命周期、暴力破解限速等）归 #36；本任务
  保证故障态下登录/改密不对 RAM 公开默认值成功、不虚报成功。
- 前端 `confirm:"FORMAT"` 知情确认契约：需产品/兼容性决策（escalation，见 §10）。
- DEGRADED 卷上全部键可读且凭据可读时 load 可返回 OK（配置本体完整）；
  任何写路径仍被门拒绝并可观测。此为有意取舍：不把"可读的真实凭据"锁死。

## 9. 涉及文件（本轮 delta 相对 92c7cdba）

- 新增：`Custom/Hal/storage_nvs_session.c/.h`、`tests/storage_gate/e2e_backend.c`、
  `tests/storage_gate/e2e_storage_boot.c`、`tests/storage_gate/gen_stubs.py`、
  `tests/storage_gate/shim/*`（主机编译 shim）。
- 修改：`Custom/Hal/storage_media_gate.c/.h`（五态）、`Custom/Hal/storage.c`
  （storage_nvs_init 收敛为会话注入）、`Custom/Core/System/json_config_nvs.c`
  （聚合助手 + load 失败语义 + 凭据信任）、`Custom/Core/System/json_config_mgr.c`
  （init/deinit/password/getters）、`Custom/Core/System/json_config_boot_gate.c/.h`
  （credential_trusted）、`Custom/Core/System/json_config_internal.h`、
  `Custom/Core/System/json_config_mgr.h`、`Custom/Core/Security/auth_mgr.c`
  （fail-closed）、`Custom/Services/Web/api/api_device_module.c`（B3）、
  `tests/storage_gate/Makefile`、`tests/storage_gate/test_storage_gate.c`、
  `Appli/Makefile`（注册 storage_nvs_session.c）。
- 未触碰 forbidden：FSBL/Frontend/Web/Model/WakeCore。

## 10. Escalation findings（交 A 决策）

1. `{"confirm":"FORMAT"}` 显式知情确认契约与既有授权前端不兼容（前端在
   forbidden 路径）。本轮已移除该要求恢复兼容；若产品仍需显式确认，需要
   A/User 排期 Web 端同步改造，不宜在 #37 单侧引入。
2. 真实目标 `storage_flash_read` 的读错误传播改进（命令模式读取或元数据
   CRC）需要真机性能/语义测量，超出隔离验证授权，未实施（§7 已给 fail-closed
   边界）。

## 11. 第二轮增量修正（Candidate `43c4d091` → 本轮，R2-B1/R2-B2/R2-B3/S1/S2）

### 11.1 R2-B1：启动可达擦除的可证明安全门

`nvs_startup` 的擦除路径（修正前逐一核对）：

| 路径 | 触发条件 | 修正前行为 | 修正后行为 |
|------|----------|-----------|-----------|
| 撕裂扫描期间的 ATE 跳过 | 活跃扇区 CRC8 非法 ATE | 无擦除，置 `TEAR_SEEN`（上一轮已闭合） | 不变 |
| 回收扇区 close_ate 为空白 | 下一扇区未 close 即含数据，`nvs_gc` 快速路径直接整扇擦除 | 未 walk 即擦除（字节保全未证明） | 启动路径已不可达：先经可证明性检查，close 空白 → 拒绝恢复 |
| 回收扇区内 CRC8 非法 ATE | GC 跳过坏条目后仍整扇擦除 | 坏 ATE 条目数据被静默丢弃 | 恢复前逐槽验证，任一 CRC8 非法 → 拒绝恢复 |
| 活跃扇区擦除（line 620） | 下一扇区非空白且无撕裂证据 | 无条件擦除活跃扇区再 GC | 仅当活跃扇区全部条目在回收扇区中存在同键同长副本时才擦除 |

新增恢复可证明性检查（`nvs_recovery_reclaim_provable` + `nvs_recovery_active_duplicated`，
均在任何擦除发生**之前**执行）：

1. **回收扇区可回收证明**：close_ate 存在（非全空白）、CRC8 合法、offset 落在
   合法条目槽区间（`offset < sector_size - 2*ate_size` 且按 ate_size 对齐），
   且 `close_ate.offset` 之上的**每一个** ATE 槽 CRC8 合法。任一不满足 →
   不可证明。论证：正常 NVS 机制中 close_ate 是扇区生命周期内最后一次写入，
   closed 扇区内出现坏 ATE 只可能是关闭后介质损坏（非撕裂），完整性不可证明；
   close 空白则该扇区提交状态完全未证明。
2. **活跃扇区副本证明**：活跃扇区（含中断 GC 的部分副本状态）内每个非空白
   ATE 必须在回收扇区中存在同键、同长、CRC8 合法的条目。存在任一未复制键
   （或 len==0 删除标记、CRC8 非法槽）→ 不可证明。论证：擦除活跃扇区后，
   GC 对回收扇区每个条目做全链活跃性判定（环回 walk 覆盖全部 closed 扇区），
   活跃条目全部是回收扇区副本时，任意时刻每把键至少存在一份完好数据
   （先擦副本 → GC 复制成功后才擦原件 → GC 写失败先于擦除返回）。
3. 证明过程中任何 flash 读错误 → 返回错误 → `nvs_init` 失败 → UNTRUSTED
   （零擦除零写入，字节保全）。

不可证明时置 `NVS_STARTUP_RECOVERY_DEFERRED` 并 rc=0 返回：会话进入
DEGRADED（写/删/清/底层范围写擦全部拒绝、读不受影响、原始字节保留），
即 A 要求的"检查并拒绝"。健康旧卷的合法启动 GC（回收扇区全量有效 + 活跃
扇区空白或纯副本，即真实可达的"close 后 GC 被断电中断"状态）不受影响，
仍按原 NVS 机制改变原始字节，验收为业务数据/有效键/凭据不丢失。

边界说明：DEGRADED 下未 close 扇区的条目对读 walk 不可达（NVS 以 close_ate
为链路），但其原始字节保留，属"合法恢复语义不丢失"；坏 ATE 条目的数据按
NVS 语义本就未提交，拒绝恢复后交由会话保护而非静默擦除。

### 11.2 S1：sector close ATE 写失败传播

`nvs_sector_close` 修正前丢弃 `nvs_flash_ate_wrt` 返回值并继续推进
`ate_wra`/`data_wra`（进而触发 GC 擦旧扇区），close 未提交时推进+擦除安全
性未证明。修正：写失败即返回该错误（`nvs_write` 的 close+GC 分支随之停止，
不发生扇区推进与 GC 擦除），并恢复 `ate_wra` 至 close 前值（否则该指针会
指向已占用槽位，后续写入会与非空白槽叠加编程）。真实介质下部分编程残留
的 close 槽在下次 close 重试或下次启动时按既有 recover/CRC 路径优雅降级
（recover_last_ate 对非法 close.offset 的扫描回退为既有机制）。
回归：close-ATE 写注入故障 → 写返回失败、零擦除、`ate_wra` 不推进、
同会话重试成功且全量数据可读（旧实现该用例失败：故障被吞、仍推进）。

### 11.3 S2：真实介质故障的会话降级与错误分类

新增 `storage_nvs_session_result_is_media_fault` / `storage_nvs_session_note_result`
（`storage_nvs_session.c`，主机可链接），`storage_nvs_write/delete/clear`
在调用 NVS 后按返回值分类（真实 `storage.c` 与 E2E 后端 shim 同步接入）：

| 返回值 | 分类 | 理由 | 会话动作 |
|--------|------|------|----------|
| `>= 0`（成功/写入长度/无变化） | 成功 | — | 不变 |
| `-EINVAL` | 良性逻辑错误 | 参数/长度越界，未触碰 flash | 不变 |
| `-EACCES` | 良性逻辑错误 | 未初始化/未 ready，未触碰 flash | 不变 |
| `-ENOSPC` | 良性容量错误 | 全部 sector close/GC 均成功后的容量上限 | 不变 |
| 其余负值（flash 读/写/擦错误，如 `-EIO`/`-1`/`-5`/`-ESPIPE` 等） | 真实介质故障 | 底层操作失败，介质完整性不可证明 | 降级 UNTRUSTED + 日志，后续 write/delete/clear 一律拒绝 |

要点：良性的容量/参数错误不会锁死会话（健康保存回归覆盖）；真实故障
（含 GC 中 flash 写/擦失败、close-ATE 写失败）在当次返回错误的同时降级
会话，杜绝同启动会话内后续写/删/清继续覆盖旧数据；降级后二次重启重新
分类。与 S1 的联动：close 写失败不再推进扇区，"未 close 扇区被运行时 GC
快速擦除"的状态在修正后不可达。

### 11.4 R2-B2：全空白首启默认凭据写失败即不可信

见 §5"凭据持久化成功判定"。隔离真实链路回归
（`test_blank_boot_credential_write_failure_failclosed`）：全空白 + 仅凭据
写入注入失败（其余首启回填写成功）→ load 失败、`config_healthy=FALSE`、
`credential_trusted=FALSE`、默认口令 verify/login/change 与
set_device_password 全部拒绝、`auth_password` 键确认未落盘、二次重启
（HEALTHY 重分类）默认口令仍被拒且原始字节保全；无故障对照
（`test_first_boot_blank_default_flow_kept`）默认登录/改密保持通过。

### 11.5 R2-B3：注释禁令回归修复

删除 `api_device_module.c` 上一轮新增的 5 行 `+ *` 文档注释，恢复基线注释
块（不删既有注释、不改 API 行为）；被删内容移入本节：
`device_storage_format_handler` 对既有受权 Web 流程的兼容说明——既有 UI
（`Web/src/pages/storageManagement`）先管理员密码验证再调用无额外字段的
POST；格式化失败时 handler 返回真实错误响应（`api_response_error`），Web
客户端可据此判定失败，卷内数据与完整性应视为未证明。

### 11.6 本轮新增回归一览

单元（test_storage_gate）：`test_recovery_clean_runs`（改为真实可达的
中断 GC 镜像：擦除计数恰 2、全键保全、恢复后可写）、
`test_startup_refuses_active_unique_data`（活跃扇区独有数据 → 拒绝恢复）、
`test_startup_refuses_reclaim_bad_ate`（回收扇区坏 ATE → 拒绝恢复，
A 指定负例）、`test_startup_refuses_reclaim_unclosed`（回收扇区未 close →
拒绝恢复）、`test_startup_recovery_read_fault_no_erase`（证明阶段读错误 →
init 失败零擦除）、`test_close_ate_write_fault_no_advance`（S1）、
`test_media_fault_mapping_and_downgrade`（S2 分类矩阵 + 降级）、
`test_session_reclaim_bad_ate_degraded`（会话级 DEGRADED + 字节保全）、
`test_boot_gate_policy_matrix` 扩展（R2-B2 持久化判定矩阵）。
E2E（test_storage_e2e）：`test_blank_boot_credential_write_failure_failclosed`、
`test_reclaim_bad_ate_degraded_preserved`（8 扇区真实链路回收扇区坏 ATE →
DEGRADED、真实凭据可登录、二次重启字节保全）、
`test_interrupted_gc_recovery_healthy`（健康启动 GC 双向回归：HEALTHY、
擦除 ≥2、业务数据 + 真实凭据保全）、`test_media_fault_downgrade_and_benign_errors`
（真实写故障 → UNTRUSTED 拒绝后续写删清 + `-EINVAL` 良性错误不锁会话）、
`test_save_first_write_failure_survives_aggregation` / 
`test_migration_write_failure_visible` 按 S2 语义更新（真实故障后同会话
拒绝、重启重分类后恢复）。判定新旧差异：以上 R2-B1/S1 用例在旧实现
（stash 验证）下全部失败，新实现下全部通过。
