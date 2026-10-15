# 实时轨迹/事件 vNext ABI 与有界资源离线可行性取证（Issue #95）

## 性质声明

本文档是独立、可按 exact-SHA 评审的 Investigation 证据，服务 A 对 Host #91 / App #27 冻结生产 WHAT 的后续决断。**本文全部候选 wire、能力位、容量与路线均不是 A 已冻结 ABI**；本文不构成任何生产准入、设备安装或 UI 真实 feed 的依据，也不为 #91/#27/#84/#25 自动补 Contract 或 READY。结论分级沿用 #87 惯例：YES（有真实有界证据支撑）/ NO（有反证）/ UNKNOWN（缺同义事实或板端实测）。

## 结论速览

| 项 | 判定 | 一句话依据 |
| --- | --- | --- |
| 现行 v3 能否等价承载真实活动轨迹/按序 IN/OUT 事件 | **NO** | App 每帧 `evts[16]` 即焚不送 Host（lc_bus.c blob 082c7ef），窗口 report 另限 12 tracks/8 points；#87 已证，本调查以夹具复现其截断/静默反例 |
| 在不改 v3 冻结面的前提下，最小兼容扩展路线是否可行 | **YES（附条件）** | 路线 B（独立受信 side-channel）不触碰 admission/manifest，夹具已验证有界队列+显式 GAP+总量可重建不变量；条件=板端资源与发布路径待实测（UNKNOWN） |
| 新签名容器/ABI major 路线是否可行 | **UNKNOWN（结构上可行、代价未冻结）** | 宿主实测 v3 manifest 192B 冻结无扩展位（reserved 184..191 仅 8B，非零即 BAD_PACKAGE），ABI 0x00040000 实测被拒；需要 Host 侧新增 manifest 布局+双 ABI 并行授予，属生产 WHAT |
| 有界 live 事件通道的内存/拷贝上界 | **YES（宿主实测+C 推导，板端 UNKNOWN）** | 候选 16B 事件、32 槽 ring=512B；tracker 全量 64 tracks=28,176B（无指针结构，ILP32 与 LP64 布局同值）；理论发布上界 10,240 B/s |
| u32 毫秒时间戳可否继续作为 live 顺序基准 | **NO** | 实测回绕处 window snapshot points=0（数据丢失）、方向闩存活跨回绕；顺序必须用回绕安全序号（`lc_serial_newer` 语义实测通过） |

## 只读源基准（实际检出版本与 blob）

| 仓 | 检出 | 文件 | blob |
| --- | --- | --- | --- |
| ne301（Host） | `experiment/app-host-poc` = `ab358eb1` | `Custom/Services/AppHost/app_admission.h` | `884477e2` |
| ne301 | 同上 | `Custom/Services/AppHost/app_admission.c` | `4cbb8477` |
| ne301 | 同上 | `Custom/Services/AppHost/app_host_session.c` | `df0e6220` |
| ne301 | 同上 | `Custom/Services/AppHost/app_host_control.h` | `ed074713` |
| ne301 | 同上 | `Custom/Common/Inc/app_host_abi.h` | `9c13b87d` |
| ne30x-app（App） | `main` = `2025db1dc3fc98b3641bcfc99976d94ac1b3db9f` | `apps/line-crossing/app/lc_bus.c` | `082c7efd` |
| ne30x-app | 同上 | `apps/line-crossing/app/lc_bus.h` | `43acf372` |
| ne30x-app | 同上 | `apps/line-crossing/include/lc_tracker.h` | `36006365` |
| ne30x-app | 同上 | `apps/line-crossing/src/lc_tracker.c` | `2b920466` |
| ne30x-app | 同上 | `apps/line-crossing/include/lc_types.h` | `327f7b2b` |
| ne30x-app | 同上 | `apps/line-crossing/include/lc_line_cross.h` | `77cf1e12` |
| ne30x-app | 同上 | `apps/line-crossing/src/lc_line_cross.c` | `f00cc60e` |

ne30x-app 仓只读（仅 `git fetch`），未修改未推送。夹具 `tests/app-live-vnext-study/` 内的 `lc_types.h`/`lc_line_cross.h/.c`/`lc_tracker.h/.c` 为上述 blob 的**注释剥离副本**（逻辑零改动，仅为满足本仓新代码零注释规范），仅作调查夹具，**不是生产 source of truth**；App 侧 `lc_bus.c` 未被复制，其调用形态（每帧 `lc_cross_evt_t evts[16]` + `lc_tracker_check_line_crossings(..., 16, &n_evts)`）在夹具 harness 中按同形复现并在此显式标注。

## AC1 基线矩阵

### Host v3 准入面（源码常量，夹具静态+运行时断言 `test_caps_probe.c`）

| 项 | 值 | 证据类 |
| --- | --- | --- |
| 容器 magic | v1 `NEAPP\x01` / v2 `\x02` / v3 `\x03`（8B） | 源码+probe |
| v3 manifest | `NMF3`，192B；publisher@4(16B)/app@20(32B)/版本@52..57/保留@58..59 必须 0/board@60/psram@64/abi@68/caps@72/exec@76/file_len@80/image_size@84/entry@88/target@92/file_sha@96/pub_key_sha@128/run_profile@160(=2)/event@164/state@168/report@172/control@176/snapshot@180/**reserved 184..191 必须 0** | 源码+probe |
| v3 ABI | `0x00030000`；函数表 60B | probe 实测常量 |
| v3 已知能力掩码 | `0x000000FF` 八位：log 0x01、tick 0x02、ai_events 0x04、report_submit 0x08、app_state 0x10、session_lifecycle 0x20、control_receive 0x40、business_snapshot 0x80。**无任何 live/trajectory/cursor 位** | probe `v3_known_caps_mask=0x000000FF` |
| caps↔容量耦合 | 任一业务/控制/快照位置位则对应容量字必须非 0，反之必须为 0 | probe 实测 |
| v3 业务 profile | NONE=0 / LINE_CROSSING_64=1 / GENERIC_FULL=2；LC64 要求六位能力全含、`event_max>=1576`、`report_max>=6144`；NONE 禁业务位 | probe 实测 |
| v3 资源下限 | `control_max>=32`（wire min）；各容量受 policy 上限钳制 | probe 实测 |

### AC1 拒绝语义实测（宿主编译真实 `app_admission.c`，mbedtls 以确定性桩替换——本 probe 只测**准入策略语义**，不测签名密码学；密码学由既有 `tests/app_admission` 以真 mbedtls 覆盖且全绿）

| 场景 | 实测结果 | 检查点 |
| --- | --- | --- |
| v3 基线（caps=0xFF，容量 1576/8192/6144/512/2048，LC64 profile） | `trusted_compatible`，verdict 全字段正确 | `v3_baseline_ok` |
| caps 加未知位 0x100 / 0x80000000 | `BAD_PACKAGE` | `v3_reject_matrix` |
| manifest abi=0x00040000（vNext major）或 0x00020000 | `BAD_PACKAGE`（parse 层拒绝，先于任何信任/资源授予） | `v3_reject_matrix` |
| reserved[184]=1（私扩字段） | `BAD_PACKAGE`（192B 冻结布局无扩展空间） | `v3_reject_matrix` |
| policy.abi_version≠0x00030000 / policy.abi_table_size≠60 / policy 缺位授权 | `ABI_INCOMPATIBLE` | `v3_reject_matrix` |
| event_max=1575 / control_max=31 / policy event_cap=1575 / snapshot_cap=1023 | `RESOURCE_LIMIT` | `v3_reject_matrix` |
| profile=NONE 且带业务位 / profile=7 | `ABI_INCOMPATIBLE` / `ERR_PARAM` | `v3_reject_matrix` |
| v2 容器带 v3 的 0x40/0x80 位 | `BAD_PACKAGE`（v2 掩码 0x3F） | `v2_caps_mask` |
| v1 容器带 v2 的 0x04 位 | `BAD_PACKAGE`（v1 掩码 0x03） | `v1_caps_mask` |

**结论（AC1-a）：YES。** 旧 v3 未声明任何 live 能力；未知能力位、未知 ABI、未知表尺寸、manifest 私扩在现行冻结规范下全部无法获得新权限。

### Host v3 会话/控制/快照面（源码读证 + app_host_session.c blob df0e6220）

- 会话 v3 表 60B、13 个受信函数（log/tick_ms/event_next/model_meta/class_name/report_submit/report_status/state_read/state_commit/should_stop/control_next/control_reply/snapshot_publish）；授予 caps `0xFF` 全八位。
- 事件 ring：8 槽（`APP_SESSION_RING_SLOTS`），满时丢头并折叠 `lost` 计数进下一事件 flags（`LOST_KNOWN`/`LOST_UNKNOWN`=0xFFFFFFFF）；`frames_dropped`/`gap_events` 计数器；`event_next` 最长等待钳 5000ms。
- 事件 wire：头 40B + 24B×≤64 检测（`APP_SESSION_EVENT_WIRE_MAX`=1576）；语义是 **OD 检测帧流**，无 track/cursor 概念。
- control/snapshot：`APP_CONTROL_MSG_RAM_MAX`=512、`SNAPSHOT_RAM_MAX`=2048、头 32B、4 类消息、stale 10s、cache 4 槽；身份=`publisher_id/app_id + session_generation + config_generation + state_revision`；`report_submit` 返回 `host_boot_id` —— 这些构成 vNext 世代字段的现成基元。

### App lc_bus/tracker 面（源码读证，blob 见上）

- 每帧 `lc_cross_evt_t evts[16]`（12B：track_id/ts_ms/direction）**栈内即焚**，只累计 `win_in/win_out` 进窗口 report；Host 不获知任何 track/事件。
- tracker：`LC_MAX_TRACKS=64`、history `LC_K_MAX=16` 点、trail 16 点（≥100ms 且位移≥0.02 或 ≥500ms 才采样）；窗口 report JSON 限 12 tracks×8 points（`LCBUS_REPORT_MAX_TRACKS/POINTS`）；report 缓冲 6144B，溢出整包丢弃（`reports_dropped`）。
- model rebind（generation 变化）→ `clear_transient`：销毁 tracker、`next_id` 跨 rebinding 携带；业务累计 `total_in/out` 持久在 state blob（`report_seq`、`config_generation` 世代化）。
- **live 数据与窗口 report 的区分**：现行系统只有(a)帧内 evts[16]（即焚、无累计可见性）、(b)窗口 report（分钟级、12×8 截断、溢出丢弃）。两者都不是实时轨迹/事件 feed；此区分是 #87 NO 结论的事实基础，本夹具予以量化复现。

### 既有回归不回退（AC1-d）

| 套件 | 结果 |
| --- | --- |
| `tests/app_admission`（v1+v2+v3+gate，真 mbedtls+真密钥向量） | 179 / 84 / 61 / 97 checks，0 failures（改造前后各跑一次一致） |
| `tests/app_report` | 552 checks，0 failures |
| `tests/app_host` validate | 21 checks，0 failures |
| `tests/app_host_session` | **NOT_RUN**：环境缺 `stedgeai_version.h`（`make model`/ST Edge AI 工具链产物，`Custom/Hal/nn.h` 依赖）；该缺失在本任务改动前即存在（本任务仅新增未跟踪文件，未触碰任何被跟踪文件），与本调查无关 |

## AC2 有界行为实验（夹具 `test_live_bounded.c`，真实 vendored lc_tracker@blob 2b920466，宿主实测）

运行：`cd tests/app-live-vnext-study && make test`。全部确定性（LCG 固定种子），691 checks / 0 failures。时间参数：33ms/帧，线 x=0.5 竖直，IN=+x。

| 实验 | 场景 | 实测锚点 |
| --- | --- | --- |
| e1_zero | 空帧穿线检查 | `events=0 win=0 total=0` |
| e1_one | 单目标两帧跨线 | `events=1 win_in=1 total_in=1 dir=IN track_id=1 ts=66` |
| e1_sixteen | 16 目标同帧跨线（cap=16） | `delivered=16 win_in=16 total_in=16 truncated=0` |
| e1_burst20 | 20 目标同帧跨线 | `produced=20 delivered=16 gap_declared=4 total_in=20 silent_loss_without_wrapper=YES` —— **业务累计正确（20），事件 feed 静默丢 4 且无 GAP 标记** |
| e1_split | 20 目标分两帧跨线（10+10） | `total_in=20 delivered=20 truncated=0` —— 截断约束是**每帧**突发，不是总量 |
| e2_confirm/depart/rebind | k_confirm 稳定门、miss 超限归档（DEPARTED 带 points）、销毁重建 | `stable_after_k_confirm=YES`、`archived=1 seg_end=departed points>=1 next_id_carried=2`、`next_id_continuity 2->2 ids_monotonic=YES` |
| e3_ramp | n=0..64 目标逐一验证 | `active_count==n for n=0..64 PASS` |
| e3_overflow | 65 检测单帧 | `input_clamped_to=64 active=64 missing_object_unreported=YES visible_counter=NONE` —— **第 65 个目标被输入钳制丢弃，且无任何可见计数器/事件可感知** |
| e3_decay | 停喂 4 帧全归档 | `active=0 archived=64 next_id=65` |
| e4_history | 200 帧 ping-pong 长轨迹，k=16 | `history_capped=YES snapshot_points=1 bound=16`（1s 采样间隔下输出点数≤k 恒成立） |
| e5_wrap_crossing | t=0xFFFFFFC0→20 跨线 | `crossing_across_u32_wrap=YES events=1` |
| e5_wrap_snapshot | 回绕后 window snapshot | `snapshot_points=0 pre_wrap_points_lost=YES from=4294967280 to=60` —— **回绕使时间序比较失效，轨迹点全丢** |
| e5_wrap_post | 回绕后往返 | `out_then_in=YES direction_latch_survives_wrap=YES events=2` |
| e5_serial_newer | `lc_serial_newer` 序号比较 | `wrap_safe_cursor_comparison=YES`（5 vs 0xFFFFFFFF 判新，1 vs 0x80000000 判旧）—— **cursor 候选语义** |
| e6_drop_oldest / drop_newest | 100 事件/帧 产、10/帧 消，50 帧 | `produced=5000 delivered=500 dropped=4478 in_ring=22 gap_declared=4478 silent=0`（两策略均有界且 `produced==delivered+dropped+in_ring` 恒等） |
| e6_hoststyle_8slot | 复刻 Host ring 8 槽丢头+lost 折叠账本 | `produced=200 delivered=106 delivered_gap=94 gapped_deliveries=4 pending_lost=0 accounting_matches_host_ring=YES loss_explicit=YES` |
| e7_consistency | 24 目标 200 帧随机游走+中途 rebind | `produced=108 delivered=108 ring_dropped=0 in_ring=0 per_frame_truncated_declared=0 rebinds=1 invariant=Held cursor_monotonic=YES` |

### 关键发现（AC2 核心结论）

1. **反例成立（NO 侧）**：`evts[16]` cap 在 >16 同帧突发时静默截断（e1_burst20），第 65 目标无声消失且无计数（e3_overflow）——"payload 有界"现状确实伴随静默丢事实。
2. **可行性成立（YES 侧）**：现有 `lc_tracker_check_line_crossings` 返回值对 `(win_in+win_out)` 与 `out_n_events` 的差值**已经暴露每帧截断量**；夹具据此在 pump 层合成显式 GAP，200 帧随机场景全程维持 `produced == delivered + ring_dropped + in_ring + per_frame_truncated` 不变量（e7）。**无需修改 tracker 即可实现非静默有界 live feed**；e7 同时证明 feed+显式 GAP 可精确重建业务累计（与 `total_in/out` 不可伪造对账）。
3. **顺序基准必须换掉 u32 ms**：回绕实测丢轨迹点、方向闩跨回绕存活；回绕安全序号（`lc_serial_newer`/`lc_serial_next` 语义）实测可用。

## AC3 双路线对比与资源预算

### 路线 A：新签名容器/ABI major（NMF4 + ABI 0x00040000 新函数表）

| 维度 | 事实/判定 |
| --- | --- |
| 当前兼容条件 | 宿主实测：abi=0x00040000 → `BAD_PACKAGE`；reserved[184]≠0 → `BAD_PACKAGE`。**v3 192B manifest 无任何可用扩展位**（184..191 仅 8B 且必须 0），无法在冻结 v3 内私加 live 容量/能力字 |
| 需要的生产改动（非本任务） | Host 新 manifest magic/布局 + 新函数表（>60B）+ 双 ABI 并行准入与 caps 授予；签名/信任链可复用 P-256 publisher 机制（probe 证明 parse 层与信任层解耦） |
| 不破坏 v3 的条件 | 容器 magic 分发链 v1/v2/v3 保持原样，v4 仅新增分支；老包零退化可保持（probe 证明 v1/v2 各自掩码独立生效） |
| 判定 | 结构上可行（YES 方向），但布局/容量/签名世代均为生产 WHAT，**UNKNOWN** 留给 A |

### 路线 B：独立受信 side-channel（不触碰 admission/manifest）

| 维度 | 事实/判定 |
| --- | --- |
| 形态 | Host 侧新增受信 producer 读取活动轨迹/事件（或 App 经新增受信出站原语推送），经有界 ring 向 Web/MQTT 投影；admission/包格式/既有八位 caps 完全不动 |
| 身份/世代设计空间（复用现有基元） | `publisher_id/app_id + session_generation`（control 已有）+ `host_boot_id`（report_submit 已返回）+ 新增 `batch_seq/cursor`（u32，`lc_serial_newer` 回绕安全语义实测）；rebind/换代在 batch 头置 `REBIND_BEFORE` 类标记（e2 证明 next_id 跨代单调可续） |
| 有界性 | 候选事件 16B、GAP 记录 16B、批头 28B（`lv_wire.h` 可编译草案）；ring 32 槽=512B（hoststyle 8 槽=128B）；丢头/丢尾/Host 式折叠三策略实测均非静默（e6） |
| 公平性成本 | 事件泵为 O(events) 内存拷贝，最坏 20 事件/帧=320B；不与 `event_next`（OD 帧 wire 1576B）、`report_submit`（6144B）、`snapshot_publish`（2048B）共享缓冲或配额（源码读证）；AI 帧流路径零改动 |
| 判定 | **YES（附条件）**：协议与有界性宿主已证；板端 RAM/CPU/发布路径 UNKNOWN |

### 成本与速率（严格分证据类）

| 项 | 值 | 证据类 |
| --- | --- | --- |
| `lc_track_t` 尺寸 | 440B（host 实测）；结构内无指针、对齐≤4，**ILP32（板端）同值** | 宿主实测+C 推导 |
| tracker 满配堆占用 | 64×440+16 = 28,176B | C 推导（布局同值） |
| 候选 live ring（32×16B+簿记） | ≈512B | C 推导 |
| Host 现有会话 ring | 8×1576 = 12,608B（对比：候选增量远小于既有预算） | 源码常量推导 |
| 轨迹全量 dump 上界 | 64 tracks×16 点×12B = 12,288B/快照 | C 推导 |
| live 持续发布上界 | trail 门 ≥100ms → ≤10 样本/s/track → 64 tracks×10×16B = **10,240 B/s** | 由 vendored tracker 常量推导（非实测速率） |
| tracker 每帧处理成本 | 24 目标 1800 帧 0.0020s 宿主墙钟（≈1.1µs/帧，含 match+crossing+ring 拷贝+消费） | 宿主实测，**不代表板端** |
| 序列化吞吐 | 5M 批×284B，20,238 MiB/s 宿主（2 次拷贝/事件=32B） | 宿主实测，**不代表板端** |
| 板端 RAM 余量/CPU/PSRAM/真实发布链路 | **NOT_RUN / UNKNOWN** | 未有真机 |

## AC4 交付：候选清单与逐项判定

> 再次声明：以下全部候选均**非 A 已冻结 ABI**，仅为可编译草案（`lv_wire.h`）与评审起点；不得据此在生产设备安装或向 UI 声称真实 feed。

### 候选 1（建议评审起点）：路线 B 最小 wire

- `lv_live_evt_t` 16B：`seq/track_id/ts_ms/direction/flags/x_q8/y_q8`；`lv_gap_evt_t` 16B：`seq/declared_gap/ts_ms/kind/flags`；`lv_live_hdr_t` 28B：`magic/header_version/reserved/batch_seq/boot_generation/session_generation/first_seq/evt_count`。
- 能力侧：不新增 caps；以 side-channel 存在性+会话授予为准。
- 丢头/丢尾/Host 式三策略可插拔；GAP 显式化由 pump 层 `(win_in+win_out)-n_evts` 与 ring 溢出共同合成。

### 候选 2：路线 A（v4 ABI major + 新表追加 live 批读原语）

- 需要 Host 新 manifest（现 192B 无位可扩，实测私扩被拒）；签名机制可复用；建议 A 若选此路线，把 live 容量/世代字段纳入新 manifest 并保持 v1/v2/v3 分发分支不动。

### 备选 3（FAIL）：复用 v2/v3 `event_next` OD 帧 wire 承载 live 事件

- FAIL 依据：wire 语义为 40B 头+24B OD 记录（x/y/w/h/conf/class），无 track_id/direction/cursor 字段位；改语义即破坏 v2/v3 冻结事件契约（`LC_EVT_*` 校验互斥）。

### 逐项判定表

| # | 项 | 判定 |
| --- | --- | --- |
| 1 | v3 可等价提供实时轨迹/按序事件 | **NO**（#87 + e1_burst20/e3_overflow/e5_wrap 反例链） |
| 2 | 路线 B 不破坏 v3 即可非静默有界 live feed（协议层） | **YES**（e6/e7 不变量 + 现有返回值可检截断） |
| 3 | 路线 A 结构可行 | **UNKNOWN**（布局/签名世代/双 ABI 授予是生产 WHAT；拒绝条件已实测） |
| 4 | 有界 ring 三丢弃策略非静默 | **YES**（e6 实测） |
| 5 | feed+GAP 可重建业务累计（不可伪造对账） | **YES**（e7 实测） |
| 6 | u32 ms 作 live 顺序基准 | **NO**（e5 反例；须序号 cursor） |
| 7 | tracker 原样复用的内存上界 | **YES**（28,176B，ILP32 同值推导） |
| 8 | live 持续带宽上界 | **YES（推导）** ≤10,240 B/s；**UNKNOWN（实测）** |
| 9 | 复用 `event_next` OD wire | **FAIL**（语义位冲突） |
| 10 | 板端 RAM/CPU/PSRAM/发布链路 | **UNKNOWN / NOT_RUN** |
| 11 | 候选=冻结 ABI / 可生产安装 / UI 真实 feed | **NO（全部）** |

### 跨仓 Host×App golden 行为矩阵（宿主实测锚点）

| 行为 | Host 侧事实 | App 侧事实 | golden 锚点 |
| --- | --- | --- | --- |
| 未知能力位 | probe：v3/v2/v1 各自掩码拒绝 | lc_app_abi_v3.h 掩码同源 0xFF | `v3_reject_matrix`/`v2_caps_mask`/`v1_caps_mask` |
| 未知 ABI | 0x00040000/0x00020000 拒绝 | `LC_APP_ABI_V3=0x00030000` 硬编码 | `v3_reject_matrix` |
| 会话 ring 满 | 8 槽丢头+lost 折叠+GAP flags | App 不感知（吃 GAP flags 侧） | `e6_hoststyle_8slot` |
| 事件截断 | （vNext 候选行为） | evts[16] 每帧 cap，差值可检 | `e1_burst20`/`e7_consistency` |
| 目标容量 | Host 无 track 概念 | 64 上限+输入钳制 | `e3_ramp`/`e3_overflow` |
| 时间回绕 | report/window 依赖 tick diff | snapshot 点丢失、闩存活 | `e5_wrap_snapshot`/`e5_wrap_post` |
| rebind/换代 | session_generation 递增 | next_id 携带、业务累计持久 | `e2_rebind`/`e7_consistency` |

### 板端仍待实测（NOT_RUN 清单）

1. 目标板 RAM/PSRAM 余量下 tracker(28,176B)+live ring(512B)+批缓冲的真实水位与最坏帧延迟。
2. live 投影经 HTTP/MQTT/Webhook 的实际吞吐、背压与丢包表现（本任务未发包）。
3. 真机 30fps 帧率下事件泵对 AI/编码/报告管线的调度干扰（公平性）。
4. H.264/编码与 preview 共享带宽时 10,240 B/s 推导上界的真实性。
5. 长跑（>49.7 天）跨回绕的实机行为验证（夹具仅逻辑级）。

## 给 A 的建议（非裁决）

1. 冻结 #91/#27 WHAT 时，把"有界 + 显式 GAP + 可对账累计 + 回绕安全 cursor"列为 live 通道的验收不变量（本调查提供全部反例与不变量测试锚点）。
2. 路线 B 以最小代价满足上述不变量且不动冻结面；若 A 需要 App 自主推送语义而非 Host 拉取，则路线 A 的 manifest/ABI 世代设计需先行冻结。
3. u32 毫秒时间戳不建议继续作为任何 vNext 顺序基准。
4. 板端五项 NOT_RUN 是任何 READY 判定的前置实测项。

## 复现

```bash
cd tests/app-live-vnext-study && make test
cd ../app_admission && make test
cd ../app_report && make test
```

全部确定性输出，预期：caps_probe 39/0、bounded 691/0、budget 8/0、admission 179+84+61+97/0、report 552/0。`tests/app_host_session` 需 ST Edge AI 生成头文件，本环境 NOT_RUN（见 AC1-d）。
