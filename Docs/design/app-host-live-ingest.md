# Host v4 有界 live ingest/read（#91 实施证据）

状态：Host 软件实现与离线 C 验证完成；目标板、生产 App 互操作、公共 read 传输均未实施（见验证矩阵）。
权威输入：issue #91 正文 contract；F8–F10 冻结评论 `repos/harryhua-ai/ne301/issues/104/comments` id `6258637748`（LVN4 wire 唯一权威）；F1–F4 id `6258534821`、F5–F7 id `6258601629`（v4 能力/会话/控制参照）。本文件所有字节语义逐条对照 F8–F10，无自造字节。

## 1. 交付物与可复核命令

| 交付物 | 路径 |
| --- | --- |
| live 受信入队/有界 ring/只读读回模块 | `Custom/Services/AppHost/app_host_live.{c,h}`（新建） |
| session 集成（真实调用点） | `Custom/Services/AppHost/app_host_session.c`（v4 策略容量、bind、`sess_api_live_publish`、teardown） |
| control 会话代际授权调整 | `Custom/Services/AppHost/app_host_control.c`（v4 bit8 拒绝、无控制面 v4 会话零配额开放） |
| live 单元套件 | `tests/app_host_live/`（新建，300 checks） |
| session 集成正负测试 | `tests/app_host_session/test_app_host_session.c`（app37 live-only 正例重写 + 新 `test_v4_live_session_ingest_read`） |
| control 开放语义测试 | `tests/app_host_control/test_app_host_control.c`（新 `test_v4_live_session_open`） |
| 构建注册 | `Appli/Makefile`（固件编译 `app_host_live.c`，一行）；`tests/app_host_session/Makefile`、`tests/app_host_control/Makefile`（链接 `app_host_live.o`） |

复核命令（worktree 根）：

```text
make -C tests/app_host_live test
make -C tests/app_host_session test
make -C tests/app_host_control test
make -C tests/app_native_vnext test
```

## 2. AC1 — 受信入队（caller 上下文与字节校验）

### 2.1 调用方授权链（全部为真实调用点）

1. 安装/启动：`app_install_query_committed` → `app_admission_verify_package`（v4 独立解析支路，含 SPKI 信任映射、native TBS SHA-256、ABI `0x00040000`、64B 函数表）→ identity 与 committed record 全等比对（publisher/app/version/`tbs_sha256`/`publisher_key_sha256`）→ `app_native_vnext_floor_check`。
2. Host 实际容量声明（`sess_fill_v4_policy`）：`supported_caps_mask = v3 known (bits0..7) | bit9 LIVE_PUBLISH (0x200)`，`live_publish_capacity = 9008`、`live_track_capacity = 64`。admission 侧既有检查（F3：bit9 要求 bit5、`live_publish_max>=312`、`live_track_max 1..64`、kind2 最坏闭包 `48+track_max×140` ≤ `live_publish_max`；超容量 RESOURCE_LIMIT）不变。
3. v4 会话代际：`app_host_control_session_open`（协议 4）分配 boot 进程内单调 `session_generation`。**改动**：v4 会话 caps 不含 bit6/bit7 时要求 `control_max==0 && snapshot_max==0` 并开放零配额会话（bit9 只要求 bit5 的 F3 语义所需的集成；此前 bit9 直接 `UNAUTHORIZED`）；v3 路径与 v4 带 bit6/bit7 路径的 `control_max>=32` 规则原样保留；bit8 `ENABLE_CONTROL` 在 v4 下仍一律 `UNAUTHORIZED`（Enable floor 与 App #30 联动非本卡范围，不给真实权限）。
4. live 绑定（`app_host_live_bind`，仅当 granted caps 含 bit9）：绑定 publisher/app、`spki_sha256`、`tbs_sha256`、`package_sha256`（install 世代身份）、`session_generation`、会话宣告的 `live_publish_max/live_track_max`（闭包二次校验）。bind 失败 → 会话整体拒绝（`live_bind_failed`），与 state/report bind 失败同型。
5. 入队回调 `sess_api_live_publish`（v4 表 slot 13，F3 签名 `int32_t(const void*,u32)`）：仅当 `container_major==4 && granted bit9 && RUNNING && !stopping` 时调用 `app_host_live_ingest(buf,len,session_generation,trusted_class_count)`；否则 `-3 UNAVAILABLE`（保持 #104 行为）。`trusted_class_count` 取当前模型视图 `class_count`（模型 rebind 后跟随当前受信类别空间）。stop/uninstall/teardown → `app_host_live_end` 清 RAM 视图；旧世代 ingest/read 一律 `GENERATION` 拒绝。

### 2.2 F8 48B header 校验（fail closed，逐字节）

| F8 字段 | 实现 |
| --- | --- |
| magic `4c564e34` | 精确匹配，否则 `HEADER` |
| header_version==1、kind∈{1,2} | 否则 `HEADER` |
| total_len==实际长度 | 否则 `HEADER`；`len<48` 或 `len>会话 live_publish_max` → `LENGTH` |
| packet_seq | 非零；重复序号仅当整包字节精确相同 → 幂等 OK（不重复入队、计数 `idempotent_replays`）；同序号不同字节或回退 → `SEQUENCE`；严格递增（首个接受包钉定基线）；`last==0xFFFFFFFF` 后任何新包 → 数据会话 `closed`（耗尽不回绕） |
| event_through_seq | 会话内非递减，回退 → `WATERMARK` |
| cumulative_event_total / cumulative_source_gap | 非递减；回退视为 u32 回绕企图 → `WATERMARK` 且数据会话 `closed`（失效停止，不静默连续） |
| config_generation | 非零；首个接受包钉定，其后必须精确相等，否则 `CONFIG_GENERATION` |
| record_count | kind1 `0..16`（>16 → `OVERFLOW`）；kind2 `0..会话 live_track_max` |
| flags bit0..2 / bit3..31 | 保留位非零 → `HEADER`；bit0 `SOURCE_GAP_DELTA` 与 `cumulative_source_gap` 增量双向一致（置位无增量、增量无置位均 `WATERMARK`）；bit1/bit2 只上浮为读状态，不当精确 GAP |
| reserved==0 | 否则 `HEADER` |

### 2.3 F9 record 校验

- kind1：`48+count×16` 精确闭合；每条 16B：`event_seq` 非零、包内严格递增、> 会话水位（禁止同身份重发成新 crossing）、≤ 本包 `event_through_seq`；`direction∈{1,2}`；`flags` 仅 bit0 允许；坐标不参与任何重算。
- kind2：逐记录解析至包尾精确耗尽（u64 偏移）：12B 头 `track_id` 非零且 snapshot 内唯一；`point_count 0..16`；`class_index==0xFFFF` 或 `< 当前 trusted_class_count`；`state==1`；`flags==0`；confidence/x/y 全宽合法。任何多余/缺失/异常 record → `LENGTH/RECORD`。
- 资源闭包：Host 声明容量 9008 = `48+64×140`（64 tracks×16 points 最坏闭包），kind1 满包 304 ≤ 9008；计数乘法全部 u64。

## 3. AC2 — 有界 ring / 最新活动轨迹只读读回

- 事件 ring：64 slot 定长（每事件 16B 语义 + Host 单调 cursor，u64 从 1 起）。ring 满丢最旧并计 `host_dropped_events`（Host-owned GAP，与 App `cumulative_source_gap` 分列，从不合并计lost）。
- 快照视图：单槽“当前全部活动 tracks”完整替换（0-track 快照合法清空视图，消失即消失，不伪造 DEPARTED）；记录 `snapshot_packet_seq`。
- 读接口（Host 内部只读，无对外传输）：`app_host_live_read(generation, after_cursor, out, cap, info)` 返回事件（含每条 cursor）与水位：`through_seq/source_total/source_gap/host_dropped_events/oldest/newest cursor/packet_count/last_packet_seq/freshness/age/loss_unknown/resync_required/session_generation/view_epoch`；空读同样返回全部水位。`app_host_live_snapshot_read` 同型返回快照与水位。`app_host_live_status` 输出身份（publisher/app/SPKI/TBS/package SHA-256）、世代、全部接受/拒绝/丢失计数（forensics）。
- 读状态合成（不伪造、不漏报）：
  - `unknown`：未绑定、数据会话 closed/exhausted、本会话无数据、最近包 `SOURCE_LOSS_UNKNOWN` 或 `RESYNC_REQUIRED`；
  - `gapped`：`cumulative_source_gap>0`（源侧已知缺失，只计一次）或读者 cursor 落在被 drop-oldest 移除的区间（Host 入队后丢失）；
  - `continuous`：其余情形。未读/被丢事实不画成连续。
- 新鲜度：最近接受包以来 `now_ms`（Host tick）>10s → `stale`，无数据 → `none`。
- 世代隔离：旧会话/换包/撤销/重启（`app_host_live_end`/init）清 RAM 视图、cursor 重置；携带旧 `session_generation` 的 read/ingest/snapshot 全部 `GENERATION` 拒绝；无任何历史 replay 通道。

## 4. 实现裁量的语义决定（均记录在此，未改动任何冻结字节）

1. `packet_seq` 采用“非零 + 会话内严格递增 + 重复需字节全同幂等 + 回退拒绝”；F8 明列的拒绝条件是“重复/回退”，严格 +1 会使任一坏包之后全部包被拒（流僵死）。跳号不授予任何额外权限，其余字节校验全部照常。
2. `SOURCE_GAP_DELTA` 一致性按双向检查（置位⇔gap 增量）；`SOURCE_LOSS_UNKNOWN` 可与精确增量并存（F10 允许部分可测部分不可测）。
3. `config_generation` 首包钉定：Host 控制面已知世代时必须一致；未知世代来源不可证实即拒绝，不引入控制面外知识。
4. kind1 `event_seq==0` 拒绝（F8 through_seq 起点 0 表示“无观测”，crossing 序号从 1 起）。
5. v4 无控制面会话（无 bit6/bit7）要求配额恰为 0 并开放会话代际分配；这是 F3 “bit9 仅要求 bit5” 在 Host 的落地；v3 行为零改动。
6. bit8 在 v4 会话/表内保持不可达（无 admin 接线、无 App #30 联动证据），防未经协调的 Enable 路径。
7. `trusted_class_count` 随当前模型视图，模型 rebind 不使已授权 live 产生错误拒绝（类别空间变化由下一次包内容自然校验）。

## 5. 资源与软件取证

| 项 | 值 | 说明 |
| --- | --- | --- |
| 事件 ring | 64×(24B 事件+8B cursor)≈2KB 静态 | 定长，无动态分配 |
| 快照视图 | 64×(12B 头+16×8B 点+对齐)≈9KB 静态 | 单槽替换 |
| 幂等重放缓冲 | 9008B 静态 | 上一接受包全量拷贝，字节精确比较（不用校验和，杜绝伪造幂等） |
| 模块静态合计 | ≈20KB | 单 mutex、单点拷贝；host 软件构建 RAM；目标 PSRAM 实占 NOT_RUN |
| CPU | 每包一次线性校验+≤64×16 点解码+≤9008B 拷贝 | 无每点堆操作；目标板主频实测 NOT_RUN |

## 6. 验证矩阵（PASS=离线 C 已证；UNKNOWN=待证据；NOT_RUN=越出本卡离线边界）

| 项 | 结果 | 证据 |
| --- | --- | --- |
| F8 header 全字段正反（magic/version/kind/total/reserved/flags 保留位） | PASS | `app_host_live` 300/0；session 671/0 |
| packet_seq 重复幂等/同号异字节/回退/跳号/0/0xFFFFFFFF 耗尽 | PASS | `app_host_live` |
| through/total/gap 水位回退、gap-delta 双向一致、SOURCE_LOSS_UNKNOWN/RESYNC 上浮 | PASS | `app_host_live` |
| config_generation 零/钉定/错世代 | PASS | `app_host_live`、session live64 |
| kind1 0/1/16/>16、严格递增、≤through、重发拒绝、方向/flags 非法 | PASS | `app_host_live`、session live64（16×单帧=16 event 包） |
| >16 crossing 守恒（16 入队 + source_gap=20 + total=56，读回 GAPPED 且 36 入队+20 源丢失=56，不伪造） | PASS | session live64 |
| drop-oldest→Host-owned GAP、慢消费者 cursor 洞、空读水位 | PASS | `app_host_live`（80 事件/16 丢/oldest=17）、session live64 |
| kind2 完整替换 0/2/64 tracks、0/16 点、class 0xFFFF 与越界、state/flags/id 重复/点数越界/尾随字节 | PASS | `app_host_live`（64 快照连续替换）、session live64 |
| 64 tracks×16 points 容量闭包（admission RESOURCE_LIMIT@9007） | PASS | `app_native_vnext` 77/0（基线保持） |
| ring 64 满负荷、queue overflow 公平性 | PASS（软件） | `app_host_live` drop-oldest 序列 |
| revoke/stop/uninstall 清视图、拒旧 cursor | PASS | session live64/app37 stop 后断言 |
| 错 signer/错世代/无权调用（v3、无 bit9 包）不能获 live | PASS | app37 enable 仍 ABI_INCOMPATIBLE；0x63 v4 probe 仍 -3；control bit8 仍 UNAUTHORIZED |
| v1/v2/v3 golden 与 report/control/Sidecar 回归 | PASS | admission 421、installer 778、state 448、run 214、sidecar lifecycle 351、report uninstall 130、vnext 77、session 671（含全部既有 v3 检查）、control 554 全 0 fail |
| App #27 signed v4 生产输出双仓互操作 | NOT_RUN | App #27 未交付；现有 app-pr37 fixtures 属 #104 证据，非 #27 产出；未以 #95 ring/mock JSON 冒充 |
| 生产 native producer 真实 tracker 字节 | NOT_RUN | 同上（无 synthetic tracks） |
| 目标板 RAM/CPU/PSRAM 实占、真机丢帧率 | NOT_RUN | 无设备 |
| NVS/Flash/掉电 | NOT_RUN | live 视图为 RAM-only，设计上不落盘 |
| 生产 live UI bridge / 浏览器读取接线 | NOT_RUN | public read 边界待后续独立冻结；本卡仅 Host 内部只读 |
| 网络发送 | NOT_RUN | 越出边界 |
| Host read 传输层（HTTP/WS 封装） | UNKNOWN | 待 #91 后续 public read 冻结后实施 |

## 7. 结果数字（2026-10-19，本分支）

- 九套件回归：admission `421/0`、session `671/0`（+79）、control `554/0`（gate 107 含内；+6）、run `214/0`、sidecar lifecycle `351/0`、report uninstall `130/0`、installer `778/0`、state `448/0`、vNext `77/0` → 合计 `3,644 checks / 0 failures`（基线 3,559 + 85 新增）。
- 新增 live 套件：`app_host_live: 300 checks / 0 failures`。
- 扩展套件附加运行：sidecar `155/0`、app_host validate `21/0`、report `552/0`、report policy `575/0`。
