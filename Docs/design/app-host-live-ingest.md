# Host v4 有界 live ingest/read（#91 实施证据）

状态：Host 软件实现与离线 C 验证完成；App #27 r3 真实 producer 同字节联合证据与真实 v4 entry 级对账已落（第 8 节）；目标板、生产密钥、公共 read 传输均未实施（见验证矩阵）。
权威输入：issue #91 正文 contract；F8–F10 冻结评论 `repos/harryhua-ai/ne301/issues/104/comments` id `6258637748`（LVN4 wire 唯一权威）；F1–F4 id `6258534821`、F5–F7 id `6258601629`（v4 能力/会话/控制参照）。本文件所有字节语义逐条对照 F8–F10，无自造字节。
血统：prior reviewed=`4ca98ed074fa0d81ca1169f1a24062e30e4eac8d`（PR #112 comment 2026-10-19T09:44:28Z REQUEST_CHANGES，阻断项=陈旧跨仓钉定 App r2 生产 entry 未 arm + 文档 PASS/NOT_RUN 失实）→ 本修正重钉 App r3 并补真实 entry 级对账，AC1/AC2 与 r2 已证的 296-check 同字节矩阵、真实 signed 会话回调/read 路径原样复用零回退；对端 App=#27 r3 交付 `c06fdedc5c7422b88d24a1e07d018dd07ff41567`（ne30x-app PR#39 r3，基座含已合并 #30 `a1b93322ae010356a6293421a137564fc0e23664`，生产 `lc_app_entry_v4.c` 已 arm+publish，全程只读）。Host 端 exact candidate SHA=本文件所在 commit（以 PR #112 Current Candidate Snapshot 与 `ght deliver` 钉定值为准）。

## 1. 交付物与可复核命令

| 交付物 | 路径 |
| --- | --- |
| live 受信入队/有界 ring/只读读回模块 | `Custom/Services/AppHost/app_host_live.{c,h}`（新建，r2 零改动） |
| session 集成（真实调用点） | `Custom/Services/AppHost/app_host_session.c`（v4 策略容量、bind、`sess_api_live_publish`、teardown；r2 零改动） |
| control 会话代际授权调整 | `Custom/Services/AppHost/app_host_control.c`（v4 bit8 拒绝、无控制面 v4 会话零配额开放；r2 零改动） |
| live 单元套件 | `tests/app_host_live/`（300 checks） |
| App #27 同字节联合套件（r2 新增） | `tests/app_host_live/test_app_host_live_joint.c` + `tests/app_host_live/app27_live_fixtures.h`（296 checks，字节+SHA-256 自校验；r3 重钉后字节全等、检查数不变） |
| session 集成正负测试 | `tests/app_host_session/test_app_host_session.c`（app37 live-only 正例重写 + `test_v4_live_session_ingest_read` + r2 `test_v4_live_app27_same_bytes_joint` + r3 新增 `test_v4_live_app_entry_r3_signed_session_joint`） |
| App r3 生产 entry 只读快照（r3 新增） | `tests/app_host_session/app27_r3/`（自 `c06fdedc…` 树 `git archive` 提取的 v4 entry+producer+tracker 源与 lcstub harness，逐字节钉定该 SHA，不改动不回推 ne30x-app） |
| control 开放语义测试 | `tests/app_host_control/test_app_host_control.c`（新 `test_v4_live_session_open`） |
| 构建注册 | `Appli/Makefile`（固件编译 `app_host_live.c`，一行）；`tests/app_host_session/Makefile`、`tests/app_host_control/Makefile`（链接 `app_host_live.o`；r2 仅加 fixtures 头 include/依赖） |

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
| F8 header 全字段正反（magic/version/kind/total/reserved/flags 保留位） | PASS | `app_host_live` 300/0；session 807/0 |
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
| v1/v2/v3 golden 与 report/control/Sidecar 回归 | PASS | admission 421、installer 778、state 448、run 214、sidecar lifecycle 351、report uninstall 130、vnext 77、session 807（含全部既有 v3 检查）、control 554 全 0 fail |
| App #27 r3 真实 producer 同字节联合（kind1/kind2/zero-track/source GAP/RESYNC 经真实 `sess_api_live_publish`/`app_host_live_ingest`） | PASS（离线 C，双仓 exact SHA 钉定） | 第 8 节；`app_host_live_joint` 296/0、session 807/0 内 `test_v4_live_app27_same_bytes_joint` |
| 同字节负例：wrong TBS、stale session、old v3、cap deficiency、slow reader、grant revoke | PASS | 第 8.4 节矩阵；全部 REJECT/UNAVAILABLE 与预期分类一致 |
| 生产 native producer 真实 tracker 字节（App r3 v4 entry 已 arm+publish） | PASS（离线） | App `lc_tracker`/`lc_bus`→`lc_lvn4` 真实链路字节（`c06fdedc…` 树内 producer，见第 8.1 节再生存证；r2→r3 字节逐位全等） |
| 真实 App r3 entry 源码与源字节→真实 signed Host 会话回调→parser/read 对账（非 live 入口环境独立模拟：lcstub+手工 live_publish 钩子；由自身已准入签名镜像执行的完整链路 NOT_RUN，v4 state/control 集成属 #113） | PASS（离线 C） | session 807/0 内 `test_v4_live_app_entry_r3_signed_session_joint`（第 8.6 节；对齐 App 侧 79-check entry test 语义，Host 侧走已证 signed 会话路径）；App 侧 `test_entry_activation` 79/0 复现 |
| 目标板 RAM/CPU/PSRAM 实占、真机丢帧率 | NOT_RUN | 无设备 |
| NVS/Flash/掉电 | NOT_RUN | live 视图为 RAM-only，设计上不落盘 |
| 生产 live UI bridge / 浏览器读取接线 | NOT_RUN | public read 边界待后续独立冻结；本卡仅 Host 内部只读 |
| 网络发送 | NOT_RUN | 越出边界 |
| Host read 传输层（HTTP/WS 封装） | UNKNOWN | 待 #91 后续 public read 冻结后实施 |

## 7. 结果数字（2026-10-19，本分支 r3）

- 九套件回归：admission `421/0`（179+84+61+97）、session `807/0`（r2 766 + r3 +41：`test_v4_live_app_entry_r3_signed_session_joint`）、control `554/0`（gate 107 含内）、run `214/0`、sidecar lifecycle `351/0`、report uninstall `130/0`、installer `778/0`（271+280+173+54）、state `448/0`（188+260）、vNext `77/0` → 合计 `3,780 checks / 0 failures`（r2 基线 3,739 + 41 新增，零回退）。
- live 套件：`app_host_live: 300 checks / 0 failures`（基线保持）。
- 同字节联合套件：`app_host_live_joint: 296 checks / 0 failures`（r3 重钉后 fixtures 字节全等，检查数与 r2 一致）。
- App 侧对账复核（只读 archive 构建，不改 ne30x-app）：pinned `c06fdedc…` 树自带 `test_host_joint` 对 pinned Host parser `610/0`、`test_entry_activation`（真实 v4 entry，79 checks）`79/0`。
- 扩展套件附加运行：sidecar `155/0`、app_host validate `21/0`、report `552/0`、report policy `575/0`。

## 8. App #27 r3 真实 producer 同字节联合证据（r3 修正，闭环 AC3/AC4）

### 8.1 来源钉定与 byte-exact 再生存证

- 对端 App producer exact SHA：`c06fdedc5c7422b88d24a1e07d018dd07ff41567`（ne30x-app `agent/27/12c88d73`，PR#39 r3；其基座含已合并 App #30 Paused `a1b93322ae010356a6293421a137564fc0e23664`；生产 `apps/line-crossing/app/lc_app_entry_v4.c` 在 entry 内 `lc_lvn4_arm`（entry_epoch profile 9008/64）+ `lcbus_set_live_observer` 接线，`api->live_publish` 经 `op_live_publish` 真实下发、gate 于 Paused）。本卡全程只读引入（`git archive` 到 /tmp 构建，不改不推 ne30x-app；session 侧另在 `tests/app_host_session/app27_r3/` 存有同 SHA 逐字节快照供对账用例构建）。prior reviewed 钉定的 r2 `da321131aceda97405a4040b54415d2d25605957` 生产 entry 不调 arm，已被本修正替换。
- r2→r3 字节等值结论（逐字节核实）：r3 相对 r2 仅改 `lc_lvn4_arm` ABI（grant→profile）、publish 失败降级路径与 gate 初值，wire 编码常量与封包格式零改动。实测 12/12 产物 SHA-256 全等于 r2 钉定值：4 个 committed fixtures（r3 树内 `fixture-sha256.txt` 与 r2 钉定常量一致）+ kind1/kind2-resync + slow-0..4 + slow 拼接（见下）。fixtures 与期望 SHA 无需更新。
- 四个 committed fixture 提取自该树 `docs/evidence/line-crossing-live/fixtures/`，字节与 SHA-256 逐字钉入 `tests/app_host_live/app27_live_fixtures.h`（数组 + 期望摘要常量；联合套件启动时对全部内嵌字节现算 SHA-256 比对，任何漂移即 FAIL）。
- RESYNC 与 slow-reader 字节：App 证据树未 committed 对应 .bin；由**同一 pinned producer 源**（`lc_lvn4.c`@`c06fdedc…` + 其自带 `live_stub`/`lc_bus`/`lc_tracker` harness）确定性驱动生成（fresh arm 后首 observe → 首包 flags bit2 RESYNC；16 轨振荡 16-crossing observe → 5×16 事件递增包）。生成器两次运行 8 个产物 SHA-256 全等（确定性），且 8/8 与 r2 钉定字节全等。
- fidelity 证明（r3 源对 pinned Host parser `app_host_live.c`，与 prior reviewed `4c1c0d7e` 逐字节相同，r2/r3 零改动）：pinned App 树自带 `tests/line-crossing-live/test_host_joint.c`（610 checks）PASS，再生成四个 committed fixture **全部 SHA-256 全等**于 App 侧 `fixture-sha256.txt`：

```text
git -C <ne30x-app> archive --format=tar c06fdedc5c7422b88d24a1e07d018dd07ff41567 | tar -x -C /tmp/app27_tree
cc -std=c11 -O2 -I<app>/include -I<app>/app -I<tests>/line-crossing-live \
  -include <tests>/line-crossing-live/live_alloc.h \
  -I<host>/Custom/Services/AppHost -I<host>/Custom/Common/Inc \
  <tests>/line-crossing-live/test_host_joint.c <tests>/line-crossing-live/live_stub.c \
  <tests>/line-crossing-live/ref_decoder.c <app>/app/lc_bus.c <app>/app/lc_bus_config.c \
  <app>/app/lc_stateblob.c <app>/app/lc_json.c <app>/app/lc_lvn4.c \
  <app>/src/lc_tracker.c <app>/src/lc_line_cross.c \
  <host>/Custom/Services/AppHost/app_host_live.c -o test_host_joint
./test_host_joint <outdir> && shasum -a 256 <outdir>/*.bin
→ 与 docs/evidence/line-crossing-live/fixture-sha256.txt@c06fdedc 全等（4/4）
```

内存宏说明：vendored `include/lc_types.h` 在未定义 `LC_MALLOC`/`LC_FREE` 时默认使用 C 标准库 malloc/free，上例即此形态（与实际工作构建 tests/app_host_session/Makefile 的无 `-D` 形态一致）。如需替换分配器，须以语法有效的单参数形式传入（如 `-D 'LC_MALLOC(sz)=live_malloc' -D 'LC_FREE(p)=live_free'`）。早期版本此行写作 `-DLC_MALLOC sz=live_malloc …`，空格会被 shell 解析为位置参数文件名，属无效命令，已废弃。

fixture 清单（字节数 + SHA-256 头 8 位缩写，全量见 fixtures 头）：

| fixture | bytes | SHA-256 | 语义 |
| --- | --- | --- | --- |
| kind1-event-batch | 64 | `590cf79c…` | 1 event（seq1/tid1/ts2800/IN/x128/y148），pkt_seq 35，through 1，gap 0 |
| kind1-source-gap | 304 | `ec1a94ae…` | 16 events（seq1..16），through 64，total 64，**source gap 48**，flags bit0 |
| kind2-tracks-64x16 | 9008 | `ba7e8108…` | 64 tracks × 16 points 满闭包快照，pkt_seq 32，through 0 |
| kind2-zero-track | 48 | `f9b0c4f6…` | 0-track 全量替换快照，pkt_seq 48，through 64，gap 48 |
| kind1-resync | 48 | `9385446b…` | 空批次仅水位（F8：空 batch 仍更新水位/状态），flags bit2 RESYNC，pkt_seq 1 |
| kind2-resync | 68 | `4336a2aa…` | 1 track × 1 point 快照，pkt_seq 2 |
| slow-0..4 | 5×304 | `56cb756f…`（拼接） | 5×16 events，through 16/32/48/64/80，gap 0，跨包严格递增 |

### 8.2 同字节正例矩阵（全部经真实 `sess_api_live_publish`（v4 signed 表 slot 13）→ `app_host_live_ingest`；session 807/0 内 `test_v4_live_app27_same_bytes_joint` 双证）

Host 侧会话 = anchor 测试密钥签名的受控 v4 包 `V4_LIVE_64_PACKAGE`（caps `0x263`=bit9 LIVE_PUBLISH+bit5，live_publish_max 9008、live_track_max 64；bind 身份来自签名 manifest：publisher `ne301-exp-pub`/app `hello-app`/TBS/SPKI/package SHA-256）。单元层 `app_host_live_joint` 以同一 owner/caps 镜像 bind。

| 输入（exact bytes） | 路径 | 期望 | 实际 |
| --- | --- | --- | --- |
| kind1-event-batch（session gen G） | signed session → live_publish | PASS；cursor=1、event_seq/track_id/ts/dir/x/y 全等、through 1/total 1/gap 0、`continuous` | PASS（session 807 + joint 296 双证） |
| 同包重放（字节全同） | live_publish | PASS 幂等（`idempotent_replays+1`，不重复入队） | PASS |
| kind1-source-gap（独立 session） | live_publish | PASS；16 events 入队，through 64/total 64/gap 48、`gapped` | PASS |
| kind2-zero-track（跟同 session gap 包后） | live_publish | PASS；快照全量清空（track_count 0，packet_seq 48），水位不回退 | PASS |
| kind2-tracks-64x16（独立 session） | live_publish | PASS；64×16 快照逐点读回（id 1..64、class 1、pts (1000,1311,27525)…(2500,1311,27525)）、`continuous` | PASS |
| kind1-resync（独立 session） | live_publish | PASS；读状态 `unknown` + `resync_required=true` 上浮 | PASS |
| kind2-resync（随后） | live_publish | PASS；1 track 快照；读状态恢复 `continuous` | PASS |
| slow-0..4（5×16 events） | live_publish | PASS；80 events 入队、ring 64 丢最旧 16 = Host-owned GAP（与 source gap 分列） | PASS |

### 8.3 层级区分（A 原文要求，如实分列）

| 层级 | 本卡证据 | 边界（本卡不含） |
| --- | --- | --- |
| signed-format parser 接受 | 8.2 全矩阵：同字节经真实 parser 逐项 PASS/REJECT | — |
| 生产 Host session grant | 受控 v4 包由 anchor **测试密钥**签名走完整 admission/session/control 链（caps/闭包/TBS 绑定全真） | 非生产密钥；生产密钥签名 NOT_RUN |
| 真实生产 entry 链路（源码级） | 8.6：真实 `app_entry`@`c06fdedc…`（已 arm）源码产出经真实 signed 会话回调入 parser/read；App 侧非 live 总线面为 lcstub 供参（同 App 自家 entry test 边界）；测试身份=Host 测试包 `ne301-exp-pub`/`hello-app`，Line Crossing entry 独立于已准入签名镜像编译（lcstub+手工钩子）——由自身已准入签名镜像执行的完整链路 NOT_RUN，v4 state/control 集成属 #113 | 目标板/NPU 实时 feed NOT_RUN |
| App #30 Paused 与 producer gate 联动 | 属 App #27 r3 交付（基座含 #30）；本卡验证 Host 对同一字节的行为与 entry Paused gate（8.6 PAUSED 相），未重复/伪造 App 状态机 | App 侧运行时行为归 #27 评审 |
| Web/UI live 读取 | 无任何 HTTP/WS/浏览器接线（public read 边界未冻结） | 属 #108，NOT_RUN |
| bit8 ENABLE_SET / floor-precommit | Host v4 路径保持不可达，本卡零增长 | 属 #113（depends_on 104,91） |

### 8.4 负例矩阵（A 列举六项，全部同字节/同路径）

| 负例 | 输入 | 期望分类 | 实际 |
| --- | --- | --- | --- |
| wrong TBS | `V4_TAMPERED_TBS_PACKAGE`（签名测试包，TBS 位翻转） | admission 拒绝 → 无会话 → live UNAVAILABLE | PASS：install ≠ OK，start `NOT_INSTALLED`，live unbound |
| stale session | stop/rebind 后携旧 session_generation ingest/read | GENERATION/UNBOUND 拒绝 | PASS：`ERR_GENERATION`；end 后 `ERR_UNBOUND` |
| old v3 | `NFC3` 48B 旧 wire 头经 live 路径；v3 会话（`V3_LINE_CROSSING_PACKAGE`） | parser HEADER 拒绝；v3 无 bit9/无 v4 表 | PASS：`ERR_HEADER`；v3 会话 granted 无 0x200、v4 probe 未调用 |
| cap deficiency | 受控签名 `V4_LIVE_PACKAGE`（312/1 最小闭包 grant）喂 9008 fixture | LENGTH 拒绝 | PASS：`ERR_LENGTH`，rejected_length+1，快照未被污染 |
| slow reader | slow-0..4（80 events）入 64-slot ring | Host-owned GAP=16，读 `gapped`，oldest_cursor=17 | PASS（source gap 仍为 0，两源丢失分列） |
| grant revoke | session stop 后 live_publish | UNAVAILABLE | PASS：`APP_SESSION_ERR_UNAVAILABLE`；read `ERR_UNBOUND` |
| 同 packet_seq 异字节 | kind1-event-batch(35) 后喂 kind1-source-gap(35) | SEQUENCE 拒绝 | PASS：`ERR_SEQUENCE` |
| 回退 packet_seq | tracks(32) 在 seq 35 之后 | SEQUENCE 拒绝 | PASS：`ERR_SEQUENCE` |

### 8.5 r2 后仍 NOT_RUN

目标板/真机摄像头/生产密钥、NPU 实时 feed、真机丢帧率与 RAM/CPU 实占、NVS/掉电（live 视图 RAM-only 设计上不落盘）、Web/UI（#108）、bit8 floor（#113）、Host public read 传输层（待后续独立冻结）。

### 8.6 真实 App r3 entry 激活→live_publish→Host parser 对账（r3 新增，补 prior reviewed 阻断项）

- 语义对齐：App #27 r3 自带 `tests/line-crossing-live/test_entry_activation.c`（79 checks，`c06fdedc…` 树内对本卡 Host parser 实测 79/0）——真实 `app_entry` 在 entry 内 `lc_lvn4_arm`（entry_epoch profile）并经 `api->live_publish` 下发；其 admission 为 callback 模拟。
- Host 侧对账用例 `test_v4_live_app_entry_r3_signed_session_joint`（session 套件内，41 checks）：v4 会话由 anchor 测试密钥签名包 `V4_LIVE_64_PACKAGE` 走**真实 admission/session/control 链**（caps `0x263`、live_publish_max 9008、live_track_max 64、bind 身份=签名 manifest 的 `ne301-exp-pub`/`hello-app`）；App 侧环境（帧/tick/model/state）由 vendored `tests/app_host_session/app27_r3/`（`c06fdedc…` 逐字节快照）的 lcstub 提供，与 App 自家 entry test 同型；stub 的 `live_publish` hook 将 entry 产出逐包委托给**已证的真实 signed 会话回调 `g_v4_api->live_publish`（slot 13）**→ `app_host_live_ingest`。
- 断言（ENABLED 相）：`app_entry == LC_APP_EXIT_OK`；`live_publish_calls>0` 且全部经真实回调 `APP_HOST_LIVE_OK`；read `returned≥1`、`through_seq==source_total==returned`、`continuous`、fresh、世代=签名会话世代、首事件 IN/seq1；snapshot 1 track fresh；stop 后 unbound、旧表回调 `APP_SESSION_ERR_UNAVAILABLE`。断言（PAUSED 相）：真实 entry 在 Paused gate 下 `live_publish_calls==0`、Host 零入包、read `none`。
- 证据边界（如实分列）：entry 的 Host 总线非 live 面（state_read/report/model/tick）由 lcstub 供参——v4 signed 会话本就无 v3 state 服务（`sess_api_state_read` 对 v4 恒 `-3`，会使 `lcbus_restore` session_fatal），与 App 自家 79-check entry test 的模拟边界一致；live 面授权（bit9/RUNNING/世代/容量闭包/TBS 绑定）全部为真实 signed 会话路径，非模拟。r2 钉定源（生产 entry 未 arm）在本用例下 `live_publish_calls` 必为 0，用例对陈旧钉定自暴露。
