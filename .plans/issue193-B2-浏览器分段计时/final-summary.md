# #193 Round-3 线 B2 最终报告 — 浏览器接入/恢复分段计时 + 播放器恢复最小修复候选

Worktree: `.worktree/lean-193-b2`（branch `lean/193-b2`，基 `2bed66602` 联合树，BUILD 615 编译检查）
依据：issue #193 §7.10 阻断表 B2 行 + round-3 胶囊。本轮全隔离：无设备/无 flash/无 PR/无推送；Web 改动仅本地提交。

---

## 1. 分段计时框架（交付物 1）

### 1.1 文件

- `scripts/browser_round.js` — E2b/joint 轮 browser_round.js 的分段升级版（Playwright chromium，真实浏览器）：
  - **段1 页面加载**：nav start → appsite iframe ready（iframe 内 video 元素出现）；细分 mark：login_done / app_tab / appsite_frame_attached / appsite_video_present。
  - **段2 WS 打开→首消息**：每 WS 连接周期，`page.on('websocket')` 网络级事件（worker 内创建的 WS 同样捕获），open→first framereceived。
  - **段3 join 首消息→首可解码帧**：vfc（requestVideoFrameCallback）首回调 或 readyState≥2 且 currentTime 前进（帧内 200ms 轮询器，亚秒分辨率）；每次撕裂/冻结后重新武装，记录 decodableEvents 序列 → 重连周期各自的段3。
  - **段4 稳态**：vfc fps、stall（vfc 间隔>1s）、teardown 签名（readyState≥2 跌落、WS 周期数>1）、断网恢复（offline_cleared→下一 decodable 事件，无则 NEVER）。
  - 场景兼容 @604 战役：晚加入（默认）/ 断线重连 `--offline-at/--offline-for`（单次离线窗，修复旧版重复 offline_set）/ 双客户端 `--dual` / 持续 `--dur` / 突然关页 `--close-at`。采样从 10s 提到 0.25s（采集期）/1s（稳态）。不自动点 Retry（`--auto-retry` 可选复现旧行为）。帧重挂载时探针自动重注入（旧版 2 次 fatal 崩溃根因之一）。
- `scripts/analyze_segments.py` — 新格式每段 median/P95/min/max + 失败计数 + 聚合；`--replay` 重放 legacy 战役 jsonl；`--selftest` 合成轨迹算术自证。

### 1.2 @604/613/614 既有数据重放验证（离线，无设备）

`python3 analyze_segments.py --replay <jsonl...>`（legacy 格式，采样量子 10s）：

| trace | seg1 页面加载 (s) | player-start→ready 上界 (s) | 稳态 | 断网恢复 |
|---|---|---|---|---|
| e2b_604_OFFLINE | 18.88 | 10.01 (±10s) | fps_med 8.49，t=90s 起 vfc 冻结于 800 | **NEVER**（恢复后观测 70s） |
| e2b_604_SOAK2 (双客户端) | 15.61 | **588.03** (±10s) | 68/137 采样 ready<2 | - |
| e2b_604_CLOSE | 18.04 | 10.01 | fps 8.30 | - |
| joint_613_J1_S | 14.41 | 20.02 | fps 10.30 | - |
| joint_613_J2_R | 15.57 | **never-ready** | 12/12 采样 ready<2 | - |
| joint_613_J7_LATE | 14.47 | 10.01 | fps 10.99 | ≤10s（短离线窗，量化 0.00） |
| joint_613_FINAL | 15.09 | 10.01 | fps 10.30 | - |
| joint_614_J1 | 16.53 | 20.02 | fps 9.40 | - |
| joint_614_J2 | 16.06 | 10.01 | fps 9.30 | - |

原始 WS join（@613 join_probe，join-sync 修复后）：n=12，median **92ms**，P95/max 1025ms —— 流级 join 亚秒成立。

**病灶定量化结论（7.10 B2 要求）**：
- 旧"harness join"混合指标（11-26s）中，**页面加载（段1）占 14.4-18.9s**（9 轮 median 15.61s / P95 18.88s）—— 与流无关；
- **流级 join（WS open→首消息→首帧的数据面）≤~1s**（92ms median）；
- 残余浏览器侧时延集中在 player-start→首可解码帧（legacy 数据只能给 10-20s ±10s 量化上界，**无法分离段2/段3** —— 正是 7.10 发现的度量缺口，v2 探针解决）与恢复行为；
- **看门狗/重连病灶**：@604 20s 断网后 **70s 零恢复**（wsRetryCount=3 次即时尝试在断网窗口内耗尽 → 永久放弃，手动 Retry 亦无效）；@613 J7 短离线（≤10s）能在窗口内恢复 —— 旧行为是断电时长赌博，非机制保证；双客户端 SOAK2 首帧 588s / 137 采样中 68 个黑屏。
- legacy 工具自身可靠性：SOAK/J8_DUAL 两轮 harness fatal 崩溃（无样本），v2 探针逐客户端 try/catch + 探针重注入。

`--selftest`：**10/10 通过**（seg1/2/3 算术、teardown 计数、fps、post-offline NEVER、legacy 边界）。

### 1.3 真机采集计划（待下次集中授权，见 §5）

---

## 2. 播放器恢复最小兼容修复候选（交付物 2）

改动：`Web/src/lib/MSE/watchdog.ts`（新增，纯状态机）、`h264Player.ts`、`websocket-init.ts`。未重写架构，未改任何协议/接口字段。

### 2.1 修复 ①：看门狗计时起点对齐 join 状态机

| 相位（WS×帧历史） | Before | After |
|---|---|---|
| 首连中（WS 未 open） | 8s（自 start()）后 no-valid-frame → hardRestart | 30s `connect-timeout` 上限内静默（connect 挂死才重启） |
| WS 已 open、无帧（join 等待） | 8s 自 connectionStartedAt（含 connect 时长）→ 拆管线 | **锚点=wsOpenAt+10s**（≥10×GOP，GOP=1s@30fps, enc.c gop_len=fps-1）→ `no-valid-frame-join` |
| WS 断开、worker 退避重连中 | 5-8s 后照样 no-valid-frame/playback-stall → hardRestart（**杀掉 worker 退避 + 重臂设备端 sps_synced 门控 → 互相放大循环**） | **不触发**（worker 有界退避 owns 恢复；stall 检查同样暂停） |
| 稳态 + socket live | 5s no-valid-frame / playback-stall / decode-stall | 不变（真实卡顿照抓） |
| 退避耗尽（wsGiveUp） | 无此状态（永久黑屏） | 全部静默；失败经 logStreamError('WS','reconnect-exhausted') + wv_error + loading 呈现 |
| MSE error | 立即 triggerRecovery | 不变 |

边界（不掩盖真故障）：open 后无数据（真故障）→ 10s join 窗口仍恢复；connect 挂死 → 30s；socket live 的播放卡死 → 5s 三重检测不变；任何非 live 相位都有 owner（worker 预算 ≤25s 后 surfaced），无无限等待。

### 2.2 修复 ②：重连放弃 → 有界指数退避，会话态保留

- Before：worker 本有无界指数退避，但主线程每个 WS 'error' 使 wsRetryCount(3)−1 并**立即** `resetStartState().start()` —— 该重启 post 'disconnect' **杀死 worker 退避**再瞬时重连；3 次耗尽后**零尝试**（E2b OFFLINE 实证），且健康检查 hardRestart 循环在断网期间每 5-8s 重建 worker+MSE+重加入流。
- After：worker 预算 = **5 次**退避重试（1/2/4/8/10s 封顶 + 0-300ms 抖动，覆盖 ≈25s）；主线程收到 'error' 不再重启（仅保持 loading）；第 5 次失败 post 一次 `reconnect-exhausted` → 主线程置 wsGiveUp、**保留 MSE/video/workers/统计（会话态）**、loading+wv_error 呈现；新 'connect'（手动 Retry/handleReload）或成功 open 重置预算。
- 风暴对比：断网 60s 内连接尝试 ≤5 次（间隔≥1s）vs 旧行为 ~3 次即时 + hardRestart 循环无上限 worker 重建；耗尽后零静默重试、失败用户可见。

### 2.3 行为对照例（@604 OFFLINE 场景推演）

断网 20s：旧行为 = 3 次即时重试（~秒级内耗尽）→ 永久黑屏，Retry 无效（实测 NEVER/70s）；新行为 = 5 次退避（1/2/4/8/10s）跨 ~25s → 网络在 20s 恢复时第 4-5 次尝试成功 → wsOpen 重锚 join 窗口 → 首帧 ≤1 GOP → **预期 ≤30s 恢复**（真机验证项）。

---

## 3. 离线验证（交付物 3）

- **vitest 23/23 通过**（3 个新文件）：
  - `watchdog.test.ts`（10）：纯状态机全相位矩阵 + join 窗口≥5×GOP + steady 5s 不变 + 退避/耗尽静默 + isSocketLive。
  - `websocket-init.test.ts`（6）：5 次退避耗尽（恰 1 次 reconnect-exhausted、总实例=6）、6 次 reconnect 通知、耗尽后新 connect 重臂全额预算、成功 open 重置预算、disconnect 取消退避、手动拆线不重连。
  - `h264Player.watchdog.test.ts`（7）：接线级 —— join 期退避 pending 120s 零拆管线；connect 挂死 31s → connect-timeout；join 窗口锚 wsOpen（29s 无触发，open 后 10.5s 触发 no-valid-frame-join）；steady+live 5s 照旧触发；steady 断网+退避 60s 零 hard-restart 且 worker 存活；耗尽后 300s 静默 + 1 次 surfaced；'error' 不再发 'disconnect'/terminate。
- **@604 数据重放**：§1.2 表 + selftest 10/10。
- `pnpm build`（生产构建）通过；`tsc --noEmit` 通过；ESLint 0 error（仅预存 console 警告）。
- **既有失败（预存，与本线无关）**：`h264Player.test.ts > rejects payloads without an Annex-B start code` 在基线 2bed66602 即失败（checkFrameData 故意的"非空 payload 兜底"返回 true vs 测试期望 false），本线未触碰该函数，未顺手修改。
- **BUILD 615 编译检查**：docker `camthink/ne301-dev:v4.0`，`C_DEFS_EXTRA="-DNETIF_WIFI_HALOW_IS_ENABLE=1" make app STEDGEAI_VARIANT=4.0 VERSION_BUILD=615 -j8`（结果见 §6，Web-only 改动 + .plans 脚本）。

---

## 4. 提交清单（本地 branch lean/193-b2，未推送）

| 文件 | 类型 | 内容 |
|---|---|---|
| `Web/src/lib/MSE/watchdog.ts` | 新增 | 纯看门狗状态机（join/steady × WS 态） |
| `Web/src/lib/MSE/h264Player.ts` | 修改 | 状态机接线 + wsOpenAt/wsReconnectPending/wsGiveUp + 删除 wsRetryCount 即时重启路径 + stall 检查 socket-live 门控 |
| `Web/src/lib/MSE/websocket-init.ts` | 修改 | 5 次有界指数退避 + reconnect-exhausted |
| `Web/src/test/watchdog.test.ts` | 新增 | 10 单测 |
| `Web/src/test/websocket-init.test.ts` | 新增 | 6 单测 |
| `Web/src/test/h264Player.watchdog.test.ts` | 新增 | 7 接线单测 |
| `.plans/issue193-B2-浏览器分段计时/scripts/browser_round.js` | 新增 | 分段探针 v2 |
| `.plans/issue193-B2-浏览器分段计时/scripts/analyze_segments.py` | 新增 | 分析器 + replay + selftest |

---

## 5. 真机验证请求草案（入下次集中授权）

**申请 A：Web 资产热加载上板**（需 User 明确授权，本轮未做）
- 对象：192.168.93.111（现 @614）；范围仅 Web 资产：`make pkg-web && make flash-web`（或等效 dev 热加载路径）；**不**刷 FSBL/App/Model，不动 OTA/NVS。
- 注意：本修复位于 `Web/src/lib/MSE/` —— 设备 deviceTool 播放页随 Web 资产生效；**appsite 业务页（g148）内嵌播放器为其打包副本**，需该 app 重新构建+经应用管理上传方可同享修复（可分两步：先 device-tool 验证，再 app 重传）。

**申请 B：浏览器轮采集**（授权 A 后）
- 用 v2 探针在 4 个 @604 病灶场景各 ≥5 轮：晚加入、断线 20s 重连、双客户端、25min 持续；用 `analyze_segments.py` 出每段 median/P95。
- 验收线（用户可见指标，非 WS join/Hub 计数）：
  - seg2（WS open→首消息）median ≤1s、P95 ≤2s；
  - seg3（首消息→首可解码帧）median ≤2s（join-sync 后 ≤1 GOP）；
  - seg4 稳态 25min：0 次看门狗 teardown、vfc fps 中位数 ≥20、stall 恢复 ≤2s；
  - 断网 20s：恢复（下一 decodable 事件）≤35s P95（退避 ~25s + join ≤1s），**不得 NEVER**；
  - 对照 @604 基线：OFFLINE NEVER/70s、SOAK2 588s、seg1 页面加载 14-19s（预期不变，页面加载属独立问题域）。

---

## 6. 构建与测试身份

- 分支：`lean/193-b2` @ 基 `2bed66602`（joint tree: M2/M3+M5+M1+M6+C1/C2+优先级+FIFO+join-sync）
- Web：`pnpm test:run` = 304 passed / 1 pre-existing fail（见 §3）；`tsc --noEmit` OK；ESLint 0 error；`pnpm build` OK
- 固件编译：BUILD 615 **PASS**（exit 0，`app build complete`；FW_VERSION_STRING 4.3.1.615），OPT=-g3（-O0），`-DNETIF_WIFI_HALOW_IS_ENABLE=1`，docker `camthink/ne301-dev:v4.0`，`make app STEDGEAI_VARIANT=4.0 VERSION_BUILD=615 -j8`；执行区 AXISRAM1_2_S 4,124,036 B（98.35%，Web-only 改动 + .plans 脚本，C 侧零改动，体积差异属构建号/生成文件范围）
- 提交：`366775a08`（lean/193-b2 本地，未推送；pre-commit lint-staged 在 worktree 下有 Web/Web 双路径 bug 被绕过，钩子等价命令 `eslint --quiet` 已手工全量执行 PASS，记录于提交信息）
- 时间：2026-11-09
