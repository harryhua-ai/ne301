# #193 Round-3 Line B2 — 浏览器接入/恢复分段计时 + 播放器恢复最小修复候选

Worktree: `.worktree/lean-193-b2`（branch lean/193-b2，基 2bed66602 = 联合树）
依据：issue #193 §7.10 阻断表 B2 行 + round-3 胶囊线 B2。隔离：无设备/无 flash/无 PR/无推送；Web 改动仅本地提交，不热加载上板。

## 任务分解

1. **分段计时框架**（本目录 `scripts/`）
   - `browser_round.js` 升级为分段探针：
     - 段1 页面加载：nav → appsite iframe ready（video 元素出现）
     - 段2 WS 打开：ws open → 首消息（page.on('websocket') 网络级事件，每连接周期）
     - 段3 join：首消息 → 首可解码帧（vfc 首回调 或 readyState≥2 且 currentTime 前进）
     - 段4 稳态：fps/stall/teardown 签名（vfc 冻结、readyState 跌落、WS 重开）
     - 场景：晚加入（默认）、断线重连（--offline-at/--offline-for）、双客户端（--dual）、持续播放（--dur）
   - `analyze_segments.py`：新格式每段 median/P95 + 失败计数；`--replay` 模式重放 @604 时代 legacy jsonl（10s 采样、无 WS 印记）→ 证明旧框架无法分离页面加载与 join（7.10 发现），并定量化病灶；`--selftest` 合成轨迹自证分段算术。
2. **播放器恢复最小兼容修复候选**（`Web/src/lib/MSE/`）
   - 新增 `watchdog.ts`：纯状态机（join/steady 相位 × WS 连接态 → none/connect-timeout/no-valid-frame-join/no-valid-frame）
   - `h264Player.ts`：no-valid-frame 看门狗改用状态机（join 期锚点 = wsOpen+10s（≥10×GOP，GOP=1s@30fps）；WS 重连退避期不触发拆管线；steady 5s 不变）；stall 检查仅在 socket live 时运行；'error' 不再即时重启耗尽 wsRetryCount（删除该放弃路径）。
   - `websocket-init.ts`：重连改为有界指数退避（5 次，1/2/4/8/10s 封顶+抖动），耗尽后 post 'reconnect-exhausted'，会话态保留（不清 MSE/不拆 worker）。
3. **离线验证**：vitest（watchdog 纯状态机 + websocket-init 有界退避 + h264Player 接线）+ @604 数据重放。
4. **报告**：分段计时表、修复 diff+单测、真机验证请求草案（下次集中授权）。

## 基线事实（@604 病灶数据，重放源）

- OFFLINE：nav→appsite_frame 18.9s；t=90s 断网，t=110s 恢复后 70s 视频全灭（ready=0/w=0/paused，2 次手动 Retry 无效）
- SOAK2（双客户端）：两客户端 ready=0 达 68/150 采样窗
- 原始 WS join（@613 join-sync 修复后）：first_msg 25-582ms 亚秒 —— 残余为浏览器/播放器侧
- 看门狗机制：h264Player.ts startHealthCheck 8s（join）/5s（steady）no-valid-frame → hardRestart（拆 worker+MSE 重连，重臂设备端 sps_synced 门控 → 互相放大）
- 重连放弃：主线程 wsRetryCount=3 耗尽即永久放弃（E2b OFFLINE 实证零重试）

## 验证

- `cd Web && pnpm test:run`（新增 3 个测试文件全过；基线 1 个既有失败 h264Player.test.ts::rejects-payloads 为 base 2bed66602 预存，与本线无关）
- `python3 scripts/analyze_segments.py --replay <@604 jsonl>` + `--selftest`
- BUILD 615 编译检查（-g3 HaLow，docker）——Web 改动不影响固件，按胶囊要求做编译身份确认
