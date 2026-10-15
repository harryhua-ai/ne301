# Issue #89 — Advanced Backlog Capacity 1..256 离线资源与恢复语义取证

日期：2026-10-15
结论级别：离线研究证据（源码事实 + 宿主测量 + 理论估算）；**不构成板端容量证明，不修改 Host 生产行为，不构成 #88 的实现授权**。

工作树基准：`95cc565481e3a46cf60c4fdd76d78c87545eb621`，分支 `agent/89/472e7da0`。
UI 基准：`counting@de25a6f1f431a631a37ff1cda87f616e293e8836:Web/src/pages/applicationManagement/lineCounting/AdvancedPage.tsx`。
受审 Host 源：`Custom/Services/AppHost/app_report_service.{h,c}`、`app_report_service_device.{h,c}`、`tests/app_report/`（全部只读，逐字节未改）。

## 结论

| 主题 | 结论 | 边界 |
|---|---|---|
| UI 数值域 1..256 | **TRUE** | `AdvancedPage.tsx` L44-51：`NumField min={1} max={256}` 写 `config.reporting.backlog_capacity`。 |
| Host 支持每 App 1..256 | **NO** | `app_report_service.h` L36 `APP_REPORT_QUEUE_SLOTS 8u` 为编译期常量；Host 全代码无 `backlog_capacity` 消费点；`app_host_session.c` 的 `report_capacity`（L1334-1336）是**每条报告字节数配额**（≤6144），不是积压条数。 |
| 静态 8 槽 × 6,240B | **TRUE（已测量）** | 宿主实测 `sizeof(report_entry_t)=6360`（payload 6240 + 120 头部）；`g_queue[8]`=50,880 B、`g_tx[8]`=256 B、seen 环 1,232 B、submit 暂存 6,240 B，队列相关静态小计 **58,740 B ≈ 57.4 KiB**（不含栈）。 |
| 256 条 RAM 下限 | **估算 1,652,148 B ≈ 1.58 MiB**（固定 16 去重视界）/ **1,670,628 B ≈ 1.59 MiB**（去重视界扩至 256） | payload 下限 1,597,440 B ≈ 1.52 MiB 与任务书一致；其余为结构/并发/暂存/栈估算。**板端 PSRAM/LittleFS/NVS 实际余量 UNKNOWN**。 |
| 256 可保证断电不丢 | **NO** | 队列是纯 RAM 静态数组；`app_report_service_restart()`（`app_report_service.c` L1090-1100）清空队列，设备适配器每次 stop（`app_report_service_device.c` L272-277）都调用它。无任何持久化路径。 |
| 吞吐天花板（worst） | **≈3.5 条/分钟**（理论） | 每条最坏发送 = MQTT QoS≥1 应答等待 2 s + webhook 同步 POST 超时 15 s = 17 s，批内串行（实测 8 条 × 2 ms+2 ms = 38.6 ms 串行排水）；容量 N 只把断连容忍从 8 分钟延到 256 分钟，不提高排水速率。 |
| 去重保证 | **视界仅 16 < 256** | seen 环 16 条（`APP_REPORT_SEEN_RING`）；实测第 17 个新 seq 后，旧 seq 可被再次接受并**重新发送**（重复投递面随容量扩大而扩大）。 |
| 三路线可行性 | R1 现状=容量锁定 8；R2 有界扩 RAM=数学可行、板端待门；R3 持久回放=唯一满足"断电不丢"，成本最高、原语缺口最大 | 详见 AC3 矩阵。 |

三种承诺必须区分：**"容量为 N 条记录"（静态布局允许）≠"硬上限内保留 N 条"（PENDING/in-flight 保留、终态可被逐出）≠"可保证断电不丢 N 条"（当前路线全部 NO）**。

## AC1 — current truth / UNKNOWN 矩阵（带代码定位）

UI 来源固定 SHA：`counting@de25a6f1f431a631a37ff1cda87f616e293e8836`。Host 来源固定 SHA：`95cc565481e3a46cf60c4fdd76d78c87545eb621`。

| # | 语义项 | 判定 | 代码定位与依据 |
|---|---|---|---|
| 1 | Backlog 数值域 1..256 用户可见 | **TRUE** | `AdvancedPage.tsx` L44-51（`backlog_capacity` NumField min=1 max=256 step=1） |
| 2 | UI 有 MQTT/Webhook 通道开关 | **TRUE** | `AdvancedPage.tsx` L24-33（`mqtt_enabled`/`webhook_enabled`） |
| 3 | UI 保存的 backlog_capacity 被 Host 消费 | **NO** | `grep -ri backlog Custom/` 无 AppHost 命中；Host 无该字段、无 API、无会话属性 |
| 4 | 静态 8 槽 × 6,240B payload 队列 | **TRUE** | `app_report_service.h` L34-36（INPUT_MAX 6144 / SLOT_CAP 6240 / QUEUE_SLOTS 8）；`app_report_service.c` L38-53（`report_entry_t.payload[6240]`）、L78（`g_queue[8]`） |
| 5 | 每条记录固定占用 6,240B payload（与实际长度无关） | **TRUE** | 定长数组槽位；实测 sizeof=6360，溢出 120B 为元数据/对齐 |
| 6 | 容量可在 1..256 内配置/运行时调整 | **NO** | 编译期常量；公共 ABI（h L100-126）无容量参数 |
| 7 | 满 8 提交返回 BUSY | **TRUE** | `entry_accept` L624-626；`tests/app_report/test_app_report.c` `test_queue_bounds` |
| 8 | 满时仅逐出最旧**终态**条目 | **TRUE** | `entry_accept` L609-616：仅 `mqtt_state != PENDING && webhook_state != PENDING` 且非 in-flight 可逐出，取 `accept_order` 最小 |
| 9 | PENDING / in-flight 不被逐出 | **TRUE** | L606-610；本研究重入测试：发送回调内第 9 条提交 → BUSY |
| 10 | 同一时间只绑定一个 App 会话 | **TRUE** | 单例 `g_session`（L74）；`app_report_service_set_session` L1108-1126 覆盖绑定；`app_host_session.c` L1617-1660 会话绑定时启动 worker 并绑定 |
| 11 | 每 App 独立队列、互不夺槽 | **NO** | 全局单队列 8 槽按 `accept_order` 共享；会话切换后旧 App 条目仍占槽但其 status 不可见（`app_report_status` L869-872 校验 app_id/key_sha） |
| 12 | MQTT/Webhook 双通道状态机独立 | **TRUE** | 条目内 `mqtt_state`/`webhook_state`/attempts/wait 独立（L44-49）；`test_channel_independence` |
| 13 | MQTT-only / Webhook-only 正常工作 | **TRUE** | 未配置通道 → `NOT_CONFIGURED` 终态，另一通道照发；`tx_send` L893-935；`test_channel_disabled` |
| 14 | 真实发送失败重试 ≤3 次 | **TRUE** | `APP_REPORT_MAX_ATTEMPTS 3`；`tx_apply` L962-970 |
| 15 | 通道 WAITING（未就绪）不计 attempts、只退避 | **TRUE** | L951-953：`*wait = APP_REPORT_BACKOFF_POLLS(4)`，attempts 不变；`test_mqtt_outage_recovery` |
| 16 | 回执 UNKNOWN → 终态 UNKNOWN、不再重试 | **TRUE** | L958-960（`APP_TX_SEND_UNKNOWN` → `*_UNKNOWN` 终态）；`test_channel_independence` L691-704（重试调用数不变） |
| 17 | 去重键 = (key_sha, app_id, report_seq)，环深 16 | **TRUE** | L82-88（`g_seen_*[16]`）、L561-589、L808-814（命中返回 OK 不入队） |
| 18 | 去重视界 16 < UI 上限 256 | **TRUE** | 本研究 dedup-horizon 测试：17 个新 seq 后旧 seq 再次被接受并**重发**（重复投递） |
| 19 | `restart()` 清空队列/统计、代际 +1 | **TRUE** | L1090-1100：memset `g_queue`/`g_tx`/stats，`g_restart_gen++`；seen 环存活 |
| 20 | restart 后旧 seq 重提交 = OK 但静默丢失 | **TRUE** | dedup 命中（seen 存活）→ 返回 OK、不入队不发送、status=UNKNOWN（L879-888 旧代际）；本研究 + `test_stop_restart_unknown_dedup` |
| 21 | 掉电/重启后报告重放 | **NO** | 纯 RAM；`boot_id` 由 UID⊕tick 生成（device.c L124-139），重 Boot 后 `app_report_status` L860-862 按 boot_id 拒绝（NOT_FOUND），seen 环消失 |
| 22 | 设备适配器 stop 必然清空队列 | **TRUE** | device.c `dev_finish_stop` L272-277 调 `app_report_service_restart()`；`test_device_lifecycle_resume` |
| 23 | stop 遇 in-flight 等待排空、失败保状态 fail-closed | **TRUE** | device.c L250-270（join 窗口下限 17,720 ms）、L333-362（QUIESCE_PENDING 保 PENDING，start 返回 BUSY）；`test_device_stop_fail_closed` |
| 24 | 发送并发模型：单飞 + 批内串行 | **TRUE** | pump L979-983（`g_tx_active` 单飞，并发 pump 计 `tx_skipped`）；L1028-1037 批内逐条同步发送；`test_timing_isolation`（发送期间 submit/status <50 ms） |
| 25 | 每条输入上限 6144B（+64B 归一化余量装进 6240 槽） | **TRUE** | submit L802-806（len>report_max 或 >6144 → QUOTA_EXCEEDED）；validate L718-720；`test_input_budget_boundary` |
| 26 | webhook 发送 = pump 线程上同步 HTTP POST，超时 15 s | **TRUE** | `webhook_service.c` L39（`WEBHOOK_TIMEOUT_MS 15000`）、L427-470（`push_json` 内联 `http_client` POST，非入队） |
| 27 | MQTT QoS≥1 等应答 2 s（250 ms 轮询）→ UNKNOWN | **TRUE** | device.c L21-26、L184-204；QoS0 即发即 OK；pump 周期 500 ms、任务栈 2048 B（L16-20） |
| 28 | 板端 PSRAM/LittleFS/NVS 实际余量 | **UNKNOWN** | 无设备访问；本卡红线禁止 Flash/NVS 破坏性测试 |
| 29 | `mqtt_service` 内部缓冲深度/底层重试 | **UNKNOWN** | `mqtt_service.c` L1599 仅转投 client（MS/Si91x），client 内部队列超出本卡受审范围 |
| 30 | 会话切换/新 App 接管后旧队列归属 | **UNKNOWN（待产品决策）** | 槽位共享、status 过滤（#11），无显式收回/迁移原语 |

反证要点：**不得把 #1 的 UI 输入当成 Host 能力**（#3 为 NO）；"已排队 8 条"已是当前硬上限（#4/#7），"保留 256"或"断电重放"在现状源码中均无路径（#6/#21）。

## AC2 — 离线量化（严格分层）

### 可复现实验

```sh
cd tests/report-policy-capacity-study
make test                          > out.txt
python3 capacity_model.py out.txt
```

C 测试白盒包含未修改的 `Custom/Services/AppHost/app_report_service.c`（宿主编译前已执行 `python3 Script/version_header.py --fsbl-version 1.0.3.1`，零仓库变更）。本次运行：**179 checks, 0 failures**；既有回归 `tests/app_report` 保持 **552 checks, 0 failures**。

### 第一层：源码事实（常量，被 C 测量交叉验证）

`SLOT_CAP=6240`、`INPUT_MAX=6144`、`QUEUE_SLOTS=8`、`SEEN_RING=16`、`MAX_ATTEMPTS=3`、`BACKOFF_POLLS=4`；设备适配器 `PUMP=500ms`、`ACK_WAIT=2000ms`、`ACK_POLL=250ms`、`STOP_JOIN=5000ms`（下限 17,720ms）、pump 栈 2048B。

### 第二层：宿主实测（本机 cc，LP64；非板端）

| 项 | 字节数 |
|---|---|
| `sizeof(report_entry_t)` | 6,360（payload 6,240 + 元数据/对齐 120） |
| `sizeof(app_tx_item_t)` | 32 |
| `g_queue[8]` | 50,880 |
| `g_tx[8]` | 256 |
| seen 环（seq/key/app/gen/used ×16） | 1,232 |
| `g_session` | 92 |
| `g_stats` | 40 |
| `app_report_submit` 暂存 `normalized[6240]`（函数静态） | 6,240 |
| **队列相关静态小计** | **58,740（≈57.4 KiB，不含栈）** |
| submit 栈上 `jr_member_t members[256]` | 12,288（宿主）/ 6,144（32 位模型） |
| 实测 8 条 × (2ms MQTT + 2ms webhook) 单批串行排水 | 38,555 µs（证明批内串行） |

### 第三层：理论估算（设备 32 位模型；capacity_model.py）

`report_entry_t` 设备模型 = 6,360 B（与宿主巧合一致：size_t 4B 抵消对齐差）；`app_tx_item_t` = 32 B。

| 容量 N | payload 下限 | 结构开销（超出 payload） | seen 固定16 | seen 去重等价 | 固定暂存+栈 | 合计（固定 seen） | 合计（去重等价） |
|---|---|---|---|---|---|---|---|
| 1 | 6,240 | 152 | 1,232 | 1,232 | 14,564 | 22,188 | 22,188 |
| 8（现状） | 49,920 | 1,216 | 1,232 | 1,232 | 14,564 | 66,932 | 66,932 |
| 16 | 99,840 | 2,432 | 1,232 | 1,232 | 14,564 | 118,068 | 118,068 |
| 64 | 399,360 | 9,728 | 1,232 | 4,928 | 14,564 | 424,884 ≈ 0.41 MiB | 428,580 ≈ 0.41 MiB |
| 256 | 1,597,440 ≈ 1.52 MiB | 38,912 | 1,232 | 19,712 | 14,564 | **1,652,148 ≈ 1.58 MiB** | **1,670,628 ≈ 1.59 MiB** |

固定暂存+栈 14,564 B = submit 暂存 6,240 + submit 栈 6,144（设备模型）+ pump 任务栈 2,048 + session 92 + stats 40。**未含**：互斥锁、发送期间任何拷贝缓冲、去重之外的元数据增长、堆碎片与峰值余量。

### 第三层：吞吐/饱和最坏模型（理论）

每条最坏发送 = MQTT QoS≥1 应答等待 2,000 ms + webhook 同步 POST 超时 15,000 ms = 17 s（两者都在 pump 线程串行执行）。

| 容量 N | 最坏单批排水 | 持续排水天花板 | 按 1 条/分钟注满耗时 |
|---|---|---|---|
| 1 | 17 s | 3.43 条/分 | 1 min |
| 8 | 136 s | 3.52 条/分 | 8 min |
| 16 | 272 s | 3.52 条/分 | 16 min |
| 64 | 1,088 s | 3.53 条/分 | 64 min |
| 256 | 4,352 s ≈ 72.5 min | 3.53 条/分 | 256 min ≈ 4 h 16 min |

到达率 > 持续排水率或队列满 → `BUSY` 丢弃（`entry_accept` 无 NULL 槽且无可逐出终态）。健康网络对照（QoS0 ~10 ms + webhook LAN ~100 ms）：8 条一批 ≈ 0.88 s，非瓶颈。**容量 N 延长断连容忍窗口（8 → 256 分钟），不改变 ≈3.5 条/分钟的 worst 排水天花板。**

### 第四层：板端 UNKNOWN / NOT_RUN

PSRAM 堆/静态区实际余量、LittleFS free bytes、NVS 余量、真实网络 P99 延迟、pump 栈高水位、`mqtt_service` 内部队列深度——全部 **NOT_RUN**（本卡禁止设备读写与真实发送）。**本研究不构成"NE301 可跑满 256"的证据。**

## AC3 — 三路线对比

每格 `判定 — 反证/依据`。反证 = 与受审源码或本研究测量冲突的具体事实。

| 行为维度 | R1 现状 RAM-only（容量 8） | R2 有界扩大 RAM 容量（≤256） | R3 受信非易失队列/回放 |
|---|---|---|---|
| 每 App 容量 1..256 可写可用 | **NO —** 编译期 8 槽（h L36），无容量 ABI | **YES（数学下限）**：256 ≈ 1.58 MiB；**板端 PSRAM 余量 UNKNOWN**，须真实设备容量门 | **YES（若实现）**：受 NV/文件配额约束；LittleFS/NVS 余量 UNKNOWN |
| 每 App 不互相夺槽 | **NO —** 单队列单会话（#10/#11），会话切换旧 App 条目仍占槽 | **UNKNOWN —** 需新增按 App 分区/归属原语；现 `entry_accept` 全局逐出会把其他 App 终态条目挤掉（L599-617 无 App 维度） | **UNKNOWN —** 同 R2，另加持久配额分配策略 |
| MQTT-only / Webhook-only | **YES —** 通道独立终态（#12/#13） | **YES —** 状态机逐条目，与容量正交 | **YES —** 同左；但回放需逐通道状态持久化（当前 `NOT_CONFIGURED` 语义在重启后不可复现 → 实现为 UNKNOWN） |
| 用户关闭通道前已排队条目 | **NO 补救 —** 已 `NOT_CONFIGURED` 终态的条目占槽直至被逐出，永不发送（pump 跳过非 PENDING，L991-993） | 同 R1（**NO**） | **UNKNOWN/YES —** 若把"通道关闭"记录为配置事件并回放时重估，可改为延后发送；属新语义，未实现 |
| 用户关闭通道前已 in-flight 条目 | **UNKNOWN —** webhook 同步 POST 不可撤回；MQTT QoS0 已出;QoS≥1 等应答中 stop 会等待排空（device.c L351）后清队列 | 同 R1（**UNKNOWN**） | **UNKNOWN —** in-flight 与持久记录的一致性（两阶段标记）为新增复杂度 |
| 超时不确定（UNKNOWN 终态） | **NO 重试 —** UNKNOWN 即终态（#16），条目占槽至逐出；`app_report_status` 返回 UNKNOWN 供 App 查询 | 同 R1（**NO**，模型不变） | **UNKNOWN —** 可选"UNKNOWN 落盘待回执补偿"，需端到端去重配合 |
| 回执丢失（对端已收但 ACK 丢） | **重复/丢失并存** — QoS≥1 视界内重试后 FAILED；QoS0 无补救；App 侧重提交在 16 视界内被去重 | 同 R1；**去重等价扩展为必需**（seen ≥ N，否则重复投递面扩大 — 本研究 dedup-horizon 实测） | **UNKNOWN —** 持久去重（持久 seen 或单调 seq）是前置原语，当前无 |
| 重启 / stop | **NO 保留 —** stop→restart 清空（#22）；重启后 status NOT_FOUND（#21）；**且重提交旧 seq 被 dedup 吞掉、返回 OK 的静默丢失陷阱**（#20） | 同 R1（**NO** — 仍是 RAM） | **YES（方案属性）—** 前提：掉电一致写入 + 回放 + 持久去重；三者均无现有代码路径（反证：Custom/ 无报告持久层） |
| 升级 / 失信 / 新 App 夺权 | **UNKNOWN —** 队列随生命周期清空故无跨版本遗留；但"新 App 绑定后旧 App 未发条目"直接消失，无补偿 | 同 R1（**UNKNOWN**） | **UNKNOWN —** 跨版本持久格式兼容、失信后旧数据处置需产品策略（#88 范围） |
| 容量调小后溢出 | **UNKNOWN —** 无容量概念，无调小路径 | **UNKNOWN —** 缩容策略未定义；可复用现逐出序（最旧终态优先、PENDING/in-flight 保留）作为建议 | **UNKNOWN —** NV 侧还需 GC/迁移 |
| 旧 report_seq 去重语义 | **视界 16** — 视界内幂等 OK；视界外重复接受+重发（实测）；restart 后 OK-but-lost 陷阱（#20） | 同 R1，**必须把 seen 扩到 ≥N**（+19,712−1,232 B @256，已计入上表） | **UNKNOWN —** 持久去重语义（boot_id 变化后 seq 单调性）待定义 |
| 资源成本 | 66,932 B 总静态（8 槽，含固定暂存/栈） | +N×6,392 B 队列+并发项；256 ≈ 1.58 MiB（**板端待门**） | R2 全部 + 持久层（文件/元数据/GC/双写缓冲）+ 回放任务/栈；LittleFS 写入寿命与 GC 停顿 UNKNOWN |
| 最坏失败模式 | 满 → BUSY 丢弃（可观测：`rejected_busy`）；断电 → 全丢但 App 可查 UNKNOWN | 断电 → 更大面积全丢（256 条）；PSRAM 越界风险需板端门 | 持久层损坏/半写 → 需校验与回退；实现错误面最大 |

三路线总结：**R1 是唯一已被源码证明的现状；R2 只改"容量为 N"承诺，不改变"断电不丢"承诺为真；R3 是唯一可能支撑"断电不丢"，但它是一个新子系统，而不是参数调整。**

## AC4 — 供 #88 的有界参数表与建议

### 有界参数表（离线证据；板端门未采集）

| 参数 | N=8（现状） | N=16 | N=64 | N=256 |
|---|---|---|---|---|
| 队列+并发 RAM（含 payload） | 51,136 B | 102,272 B | 409,088 B | 1,636,352 B |
| 含 seen/暂存/栈 总静态 | 66,932 B ≈ 65 KiB | 118,068 B ≈ 115 KiB | 424,884 B ≈ 0.41 MiB | 1,652,148 B ≈ 1.58 MiB |
| 去重等价（seen ≥ N）总静态 | 同左 | 同左 | 428,580 B | 1,670,628 B ≈ 1.59 MiB |
| 断连容忍 @1 条/分 | 8 min | 16 min | 64 min | 256 min |
| 最坏单批排水 | 136 s | 272 s | 1,088 s | 4,352 s |
| 持续排水天花板（worst） | ≈3.5 条/分（与 N 无关） | 同左 | 同左 | 同左 |
| 断电不丢 | NO | NO | NO | NO |
| 板端余量门 | NOT_RUN | NOT_RUN | NOT_RUN | NOT_RUN |

### 建议 / 备选

- **建议（保守）**：#88 先按 R1 落决策——UI `backlog_capacity` 必须映射真实 Host 容量或明确显示"当前设备上限 8"；在板端余量门（见下）采集前，不承诺 >8。
- **备选 A（R2）**：若板端确认 PSRAM 余量 ≥ ~2 MiB 峰值且产品接受"断电即丢"，可分级放开 16 → 64；256 级建议默认关闭、按设备能力枚举暴露。前置：去重 seen 扩至 ≥N、容量缩小时逐出序沿用"最旧终态优先、PENDING/in-flight 保留"。
- **备选 B（R3）**：仅当 #88 明确"断电不丢"为产品承诺时立项；作为新子系统单独评审（持久格式、崩溃一致、回放、持久去重、配额门），不得以本次离线结果替代其设备验收。
- **不建议**：以临时 1..8 限制冒充已满足旧 UI 产品意图（契约明令）；或宣称 256 现已可用（与 #3/#6 反证冲突）。

### 可证伪测试（未来 Host 实现时）

1. **容量上界**：板上以 N=256 连续提交 300 条（1 条/分注入）→ 出现 `rejected_busy>0` 或 alloc 失败即证伪"256 可用"。
2. **R2 断电语义**：256 队列 PENDING 状态断电重启 → 队列 count=0、全部 UNKNOWN：证明"断电不丢"为假（R2/R1 预期）。
3. **R3 前提**：掉电注入（写一半断电）后回放零丢失零重复 → 任一丢失/重复即证伪受信持久队列。
4. **去重视界**：N+1 个新 seq 后重提交第 1 条 seq → 若仍被去重则视界 ≥N 成立；实测现实现第 17 个即失效。
5. **排水天花板**：双通道注入人为 2 s/15 s 时延 → 8 条批排水实测 ≥136 s 量级即验证串行模型。

### 板端待采集项（本卡 NOT_RUN，#88 设备门前必采）

PSRAM 静态/堆余量与碎片、LittleFS free bytes 与 GC 停顿、NVS 余量、`mqtt_service` 内部队列深度与 QoS1 实测 ACK 率、webhook 实测 P99、pump 线程栈高水位、256 条注入下整机 `sys mem` 表现。

### 未来 Host 原语最小缺口

1. 运行时容量参数：会话级 `backlog_capacity` 字段 + 校验 + 与 UI 值的绑定路径（现无，#3/#6）。
2. 按 App 队列分区/归属与互不夺槽策略（#11，现全局逐出无 App 维度）。
3. 容量缩小时的逐出序定义（建议沿用 accept_order 最旧终态优先）。
4. 去重视界 ≥ 容量（现固定 16）。
5. （仅 R3）持久队列 + 崩溃一致写入 + 回放 + 持久去重 + NV 配额门——全部缺失，最大缺口。

## NOT_RUN / UNKNOWN 清单

- NOT_RUN：真机 PSRAM/RAM 余量、LittleFS/NVS 容量与写入、真实 MQTT/Webhook 网络发送、板端 256 注入、pump 栈水位、`mqtt_service` client 内部队列行为。
- UNKNOWN：会话切换/新 App 接管后旧队列归属策略（#30）、容量调小溢出策略、R3 全部实现语义、UI `backlog_capacity` 与未来 Host 字段的映射。
- 明示边界：本研究全部宿主测量在 macOS LP64 (`cc -std=c11 -O1`) 完成，不等价于 STM32N6 板端；理论模型不升级为测量。

## 边界声明

本研究未修改 `Custom/**`、`Appli/**`、`Web/**`、`Frontend/**`、`FSBL/**`、`Model/**`、`WakeCore/**`、根 `Makefile`、`.github/**`、`scripts/**`；未向实际网络发包，未执行设备读写或 Flash/NVS/LittleFS 破坏性操作；既有 `app_report` 回归未回退（552 checks, 0 failures）。新增物：`tests/report-policy-capacity-study/`（C 受控测试 + 模型脚本 + Makefile）与本文档。
