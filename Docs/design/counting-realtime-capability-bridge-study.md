# Counting 实时能力桥接调查（Issue #87）

## 结论

**整体 YES/NO/UNKNOWN：NO。** 现有受信路径可真实提供 App 配置、窗口/累计计数、会话/模型摘要和按单份报告查询的 MQTT/Webhook 结果；不能按旧 counting Web 的 2 秒语义提供活动轨迹和有序事件列表，也没有旧 Advanced 页所需的逐 App 传输开关、队列容量写入和每通道历史 drop 数。

Host 已有的 `GET /api/v1/app/business/snapshot` 是真实的、受高权限管理员认证的浏览器读取接口。它返回当前受信会话的有界 App 快照及身份、新鲜度和年龄。这个发现支持配置与 aggregate stats 的部分保留；它不改变实时轨迹/事件的 NO 判定。不能把窗口结束后才生成的 report 接成旧 Web 的 Realtime 页面。

最小核心差额是一个**当前 App 会话身份绑定、可标注新鲜度与缺口的有界实时 counting 读取原语**：让 Host 可读到活动轨迹/points 和带稳定序号、时间、track ID、方向的穿线事件，并以受限 HTTP projection 提供给用户界面。它属于新的 App→Host 语义/通道；本调查没有定义或实现 wire、ABI、端点或 UI。传输策略和旧 `enable` 另有 Host 语义差额，交由 A 在 #79 冻结 WHAT。

## 基准与判断规则

- 旧 Web 锚点：`ne301/counting@de25a6f1f431a631a37ff1cda87f616e293e8836`。
- App 锚点：`ne30x-app/main@9a5db17cfba6f8ab52d17b031784e71c66a5ab32`。
- Host 锚点：本调查基线 `ne301@95cc565481e3a46cf60c4fdd76d78c87545eb621`，包含 #56/#75。
- **YES** 表示现有受信 producer/API 已真实承载且有界；**NO** 表示缺字段、producer、读取面或与旧语义冲突；**UNKNOWN** 表示存在相邻事实，但缺少同义映射或可授权浏览器面。`报文有字段`与`Web 可读`分列，不互相推导。

旧 Web 的 `line-counting.ts` 定义 `getConfig/getStatus/getStats/getEvents/getTracks/reset`。Realtime 页每 2 秒并行拉取 status、stats、events、tracks，空缺配置时额外取 config；它把 tracks.points 转成视频叠加折线，并按 event sequence/time/track/direction 显示 IN/OUT。三页分别是 Realtime、Config、Advanced。

## 现有受信路径

| Host API | 权限与返回 | 能力边界 |
| --- | --- | --- |
| `GET /api/v1/app/business/snapshot` | 路由需要认证，handler 另要求 high-privilege admin；返回 `availability`、`publisher_id/app_id`、session/config/state generation、`age_ms`、`payload_len` 和原生 snapshot payload | 只读当前受信 App 快照；无 report payload 或 tracks/events 查询。stale 时仍返回带 stale 标签的 payload，调用方必须拒绝把它标成当前值 |
| `GET /api/v1/app/session/status` | 认证 + high-privilege admin；返回 App/session 身份、ABI/caps、`app_running`、model name/version/generations/class_count 和 session counters | 有会话/模型摘要，不是 counting `enabled/state/reason`，没有 class ID/name 列表 |
| `GET /api/v1/app/control/status` | 认证 + high-privilege admin；返回 session、control/snapshot limits、command/receipt 概况 | 控制命令状态，不是 MQTT/Webhook 报告状态 |
| `POST /api/v1/app/control/config`、`POST /api/v1/app/control/reset` | 认证 + high-privilege admin；当前会话、generation CAS、App 回执 | 可提交 config-only 或确认后的 counter reset；不会写 Host 传输策略 |
| App trusted session 的 `report_status(host_boot_id, report_seq, ...)` | App 按 boot ID + report sequence 查询一份报告的 MQTT/Webhook 两通道状态 | 有界、按 report 查询；不是 Web endpoint，也不返回历史 drop/backlog aggregate |

当前 Web endpoint 清单列有 4 条 App Control 路由，没有旧的 `/api/v1/apps/line-counting/events`、`tracks`、`stats`、`config` 或 report payload 查询路由。`app_report_service_entry(index, ...)` 只返回 boot/seq、两通道状态、attempts、payload length；不返回 payload。Host `queue_count`/entry 可在 Host 内最多枚举 8 个槽，但不是已授权的浏览器 API。

## YES/NO/UNKNOWN 能力矩阵

| 旧 Web 字段/动作 | 现有真实 producer / 身份 / 频度 / 窗口含义 | App/Host 有界数据 | 当前认证 Web 读取/写入 | 旧 Web 同义判定 |
| --- | --- |:---:|:---:|:---:|
| `counter_name`、`target_class` | App v3 `active_config`；会话绑定 `publisher_id/app_id`；snapshot 至少每 5 秒发布，配置应用后可强制发布 | YES | YES：snapshot 读取，config command 写入 | YES，遵守 generation/receipt；不是 2 秒源刷新 |
| `line{x1,y1,x2,y2,outside_x,outside_y}` | App v3 以 0..1000 permille 保存/投影；旧 Web 转换为 normalized 数值 | YES | YES：snapshot + config command | YES，按 permille 转换；不是逐像素无限精度 |
| `confidence_threshold`、`tracking.association_distance/history_length/max_missed_frames/confirmation_frames` | App v3 对应 `conf_threshold_permille/max_dist_permille/track_history_k/max_miss/k_confirm`；App 是唯一业务校验者 | YES | YES：snapshot + config command | YES，旧 UI 可用范围可表示 |
| `window_minutes` | App v3 active config；取值 1..1440；决定 report 窗口边界 | YES | YES：snapshot + config command | YES；不能解释成 Realtime 刷新周期 |
| `reporting.tracks_enabled`、`heat_grid_enabled` | App v3 config toggles；控制是否把 tracks/heat_grid 写进窗口 report | YES | YES：snapshot + config command | YES，仅表示“是否报告窗口历史”，不表示存在实时轨迹读取 |
| `enable`、`status.enabled` | config-only 144B 和 `line-crossing/1` payload 均无业务 enable；Host session `app_running` 是 native App 生命周期 | NO | NO：没有业务 enable 字段 | NO；不能映射为 start/stop App |
| `status.state/reason/resetting` | snapshot 有 `app_state=running/unbound/restricted`、storage state；session status 有 session state；与旧 `running/disabled/unsupported_model/target_class_invalid` 不同 | UNKNOWN | UNKNOWN：有相邻状态，但没有同义 counting reason/resetting projection | UNKNOWN；不能宣称一一对应 |
| `status.model.name/version/generation` | `/app/session/status` 给 model name/version/model/class generation | YES | YES：high-privilege session status | YES（对应值）；与旧 status 同一采样时刻未做端到端浏览器验证 |
| `status.model.classes[{id,name}]`、`postprocess_type` | Host session 有 class-name 回调给 App，但 HTTP session status 只给 class_count；snapshot 只给当前 target class | NO（完整列表） | NO | NO；当前接口不能填充旧类列表 |
| `getStats.window/total` 的 IN/OUT | App `counters`/`cumulative` snapshot；窗口 reset 与 App 窗口边界一致 | YES | YES：snapshot | YES，带 freshness 标签；5 秒源更新，stale >10 秒 |
| `getStats.delivery.*.backlog` 当前队列占用 | Host 8 槽队列元数据及逐条通道 pending 状态存在；Host 内可有界计算当前 pending 数 | YES（Host 内） | NO：无 report summary HTTP route | NO（现有 Web 路径）；不能把 snapshot `report_seq` 当 backlog |
| `getStats.delivery.*.dropped` 每通道历史数 | Host 有 `rejected_busy/rejected_quota` 等 aggregate；App 内部有 reports_dropped，但 snapshot 不投影；无每通道历史 drop counter | NO | NO | NO |
| 单份 report 的 MQTT/Webhook 状态 | App/Host trusted `report_status(boot_id, seq)`；Host 队列 entry 给 pending/succeeded/failed/unknown/not-configured 元数据；report 完成槽可逐出 | YES | NO：没有浏览器按 report ID 的读取面 | NO（旧聚合 stats 不等同单报告状态） |
| `getEvents`: sequence、timestamp、track_id、direction、server_now_ms | v3 snapshot 无 events。窗口 report 的 `tracks[].events` 可有 `line_cross_in/out`，但只在窗口关闭时提交，缺旧 events list/cursor 等价 | YES（仅窗口 history 字段） | NO：Host report entry 不返 payload | NO：不支持 2 秒实时事件列表 |
| `getTracks`: 活动 track_id 与连续 points | v3 snapshot 无 tracks。真实 `lc_bus.c` report builder 在 `tracks_report_enable` 时最多报告 12 个 track、每 track 最多 8 个 points，窗口关闭后提交 | YES（仅窗口 history 报文） | NO：无 payload 读取 API | NO：不能用于 2 秒视频轨迹叠加 |
| `mqtt_enabled`、`webhook_enabled` | Host 复用设备级 MQTT/Webhook 配置；没有逐 App 两开关在 `CONFIG_APPLY` 内原子保存的字段 | NO（逐 App） | NO | NO；设备通道 configured/ready 不是旧开关 |
| `backlog_capacity` 可写 1..256 | Host `APP_REPORT_QUEUE_SLOTS=8` 固定；无 per-App capacity setter，队列不持久化 | NO | NO | NO；不能把 8 槽报告队列说成可调 backlog |
| `reset` counters | v3 `COUNTERS_RESET`，要求 `expected_config_generation` 和 `destructive_confirmed`，返回 App receipt | YES | YES：high-privilege POST | YES，须保留确认/回执语义 |
| 视频上的活动轨迹覆盖 | 旧 Web `/stream` 视频独立于 `GET /tracks`；轨迹 overlay 由 `getTracks` 输入 | 视频来源与本调查的轨迹读取无关 | UNKNOWN（未做真机视频桥接验证） | NO：已有视频流不构成轨迹数据 producer |

因此，**complete mature counting Web parity = NO**。配置、窗口/累计统计和 reset 有可用基础；显示可用状态必须检查 freshness/identity。事件/轨迹的报文字段存在，不等于有可读的实时 API。

## 时间、身份与容量事实

- 旧 Web 以 2,000ms timer 轮询 status/stats/events/tracks；这只是调用频度，取决于服务端数据源。
- App v3 snapshot 声明 `snapshot_max=1024B`；App schema 的当前最坏合法快照实测为 981B（含 32B 控制头），Host 快照 RAM ceiling 为 2,048B，超过签名额度或 Host cap 会整体拒绝。快照周期至少 5 秒；Host age 大于 10,000ms 标 `stale`。2 秒轮询只会多次读同一快照。
- CONFIG_APPLY 的业务 config-only payload 是固定 144B（32B 统一头 + 144B = 176B 完整命令）；v3 控制候选只接受配置字段。App 私有状态 blob（152B/160B）不是 config，App 解码路径会拒绝把它当候选。
- App 窗口周期为 `window_minutes * 60,000ms`；`maybe_close_window` 在事件或 idle 路径观察到阈值后才关闭窗口。1 分钟是最小窗口；实际任务循环触发边界后的最大迟延未由本 harness 测出。report 包含 start/end monotonic ms，`clock_valid=false`、`reported_at=null`，不是旧 UI 的 event timestamp/server-now feed。
- App report buffer `LC_APP_REPORT_MAX=6,144B`；report tracks cap 为 12 条、每条 points cap 为 8。report overflow 时整条 drop，不截断。
- Host 报告输入上限 6,144B；规范化 slot 为 6,240B，容纳最多 +59B 规范化增量。8 个 payload arrays 共 49,920B，加队列条目元数据；不是持久 backlog。8 个 pending 槽占满时新报告返回 BUSY；终态槽可逐出最旧项。重启后旧 boot ID/sequence 查询 NOT_FOUND。
- Host report service 可按 boot ID + sequence 查询每份 report 的每通道状态；App 在 idle path 轮询未决状态，最多 240 次后记 unresolved。状态结果只保留在有界易失队列/当前 App 计数器，snapshot 不含旧 Web backlog/dropped。
- GET snapshot/session/control/status 需要高权限管理员。响应有当前 publisher/app identity，但不是给独立 App 的低权限能力，也不授权把管理员凭据交给 App。

## 离线行为取证

复跑入口：`tests/line-counting-bridge-study/run.py`。它要求本地 Git object database 已含 App 固定 SHA；先只读获取源码，再运行 harness：

```sh
git remote add ne30x https://github.com/harryhua-ai/ne30x-app.git
git fetch ne30x 9a5db17cfba6f8ab52d17b031784e71c66a5ab32
python3 tests/line-counting-bridge-study/run.py
```

脚本将固定 App 源文件 archive 到临时目录，直接编译并执行真实 `apps/line-crossing/app/lc_bus.c`、tracker、line-cross、JSON 和 App entry 源；输入是测试 stub 经 App tracker 消费的离线检测向量，不是模拟的输出 JSON，也不是相机/线上数据。Host 一侧编译当前真实 `app_report_service.c`、`app_report_service_device.c`，并复用现有 `tests/app_report` queue/status harness；测试 transport/identity 是本地 stub，不会联网。临时源码、二进制和报告运行结束自动移除。

本次结果：

| 用例 | 观察结果 |
| --- | --- |
| tracks reporting on；真实 IN + OUT 交叉后在 tick 61,000ms 关闭窗口 | App 生成 1,094B 合法 report；`window.in=1`、`window.out=1`，2 条 tracks、合计 2 个 points；包含 `line_cross_in` 与 `line_cross_out`；report ≤6,144B |
| tracks reporting off；相同 crossing 和窗口 tick | App 生成 727B report；保留 IN/OUT 窗口计数，没有 `tracks` 字段 |
| 把 tracks-on 的真实 App report 交给当前 Host `app_report_submit` | Host 接受；normalized queue payload 1,097B；8 槽中的 1 槽；MQTT/Webhook 各为 PENDING |
| 从真实 report 派生合法 JSON whitespace padding 到 6,145B | Host 返回 QUOTA_EXCEEDED；未入队 |
| 换成不同 session key 后用旧 session 提交；再撤销当前 session | 均为 UNAUTHORIZED |
| Host 现有 queue/status 全套 | `app_report: 552 checks, 0 failures`；覆盖 8 槽满/第 9 个 pending BUSY、终态最旧逐出、boot 重启后旧 status NOT_FOUND、身份变化、通道 pending/success/fail、6,144B/6,145B 边界 |
| Host control/snapshot + HTTP route gate | `app_host_control: total=441 fail=0`；`app_control_gate: total=107 fail=0`，覆盖 >10 秒 stale 与 snapshot route 高权限门 |
| Host session 回归 | `app_host_session: total=540 fail=0`。环境缺 `stedgeai_version.h`；使用 `/tmp` 空 include shim 仅满足 `nn.h` 的无行为 include 后通过，没有改仓库或 NN 版本逻辑 |

上述 track 数/points 数是本次输入向量经过真实 App tracker 的观测，不是源代码最大值。测试证明 report 生产路径存在，不证明真机跟踪质量、摄像头当前目标、Web 2 秒刷新或网络投递。

## 限制、安全反例与 UNKNOWN

- NOT_RUN：真机摄像头/模型、安装身份真实切换、设备 Web HTTP 端到端、真实 MQTT/Webhook 投递、掉电/设备重启、真实 UI 视频叠加。无设备读写、无外网 report。
- 测试 stub 证明 Host 对传入 session identity/active state 的拒绝路径；它不是真实发行者签名/板上身份/权限授予的替代证据。
- `line_crossing` report 的 `tracks[]` 是**窗口历史**；其 points 数量可能很少，track 仅在 `close_window` 生成时读取。把该 payload 每 2 秒重复返回，会重复旧历史，不会成为活动轨迹。
- 队列 status 可因终态逐出或新 boot 而消失；当前统计没有每 channel 历史 dropped。不能以当前 queue count 推回已丢掉的历史总量。
- App `enable` 和 Host `app_running` / `app_state` 权属不同。停止 native App session 不证明“仅关闭 counting”，也可能影响它持有的其它能力。
- Snapshot 超过 1,024B 声明额度、Host 内存不足或 stale 时，必须保留 `unavailable/stale`，不显示最后值为实时；本轮没有用私有 state blob 或超额 JSON 去扩展 snapshot。
- 当前 Web 读取路由由高权限管理员控制；它不是独立 UI 的 App-scoped least-privilege grant。report queue C helper 可被 Host 内部代码调用，不代表已经存在浏览器可读接口。

## 给 #79 的 WHAT 建议

1. 不冻结“旧 Realtime 轨迹/事件等价已完成”。现有 snapshot 支持配置与统计；窗口 report 应保持为单独的历史/报送语义。
2. 若要保留旧 2 秒 Realtime 用户目标，由 A 先决定是否需要新版本/通道：最小业务原语是会话绑定、有限容量、带事件 cursor/时间/track ID/direction、活动 track points、新鲜度及 gap/overflow 的 App-owned live counting projection，并由 Host 在同一受信身份下提供只读查询。payload 上限、保留策略与具体 wire 留待独立合同。
3. 单独确定 Host-owned 每 App transport policy/read summary：旧 MQTT/Webhook enable、逐通道 backlog/drop 不是当前 App config，也不是设备全局通道状态；当前固定 8 槽不等于旧可写 1..256 backlog_capacity。
4. 单独决定业务 `enable` 是否需要区别于 native App session start/stop。调查没有把 session lifecycle 偷换成 counting enable。

## 验收映射

- [x] AC1 — 固定旧 Web、App v3 schema/snapshot、Host HTTP/session/report API 的字段、producer、身份、频度、窗口和缺口逐项入矩阵；Web report payload 可见性与报文字段分开。
- [x] AC2 — 真实 App `lc_bus.c` 窗口路径 on/off、IN/OUT、points、大小、窗口 tick 与 Host queue/status、满队列、终态逐出、重启、换身份、失权、超配额均有可复跑证据；另运行 stale snapshot 回归。旧 2 秒 Realtime 与窗口历史差异明确。
- [x] AC3 — 明确 1,024B snapshot 声明、144B config-only、Host 2,048B snapshot RAM、6,144B report input、6,240B slot、8 槽 payload 上限；不把私有 blob、transport config 或 window report 冒充实时/配置等价。
- [x] AC4 — 新增可复跑离线 harness 与本报告；说明安全/资源反例、NOT_RUN/UNKNOWN 与最小语义缺口。没有修改生产 App/Host/Web、冻结 v3 ABI、外部仓库文件或真实设备状态。
