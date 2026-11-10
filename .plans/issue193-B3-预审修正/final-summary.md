# #193 B3 最终报告 — 预审两缺陷修复 + 交付身份校正

基线：`bba5baf2cfd95b826c00f43eeabb90f534041c5c`（远端 `pre-review/193-final-candidate`，审计身份保留）。
本轮全隔离：无设备/无 flash/无 OTA/无正式 PR；唯一推送 = 同名预审分支一次 fast-forward（新远端 SHA 以 #193 评论报告）。

---

## 1. 预审两缺陷修复（commit `6c3dc98b60bac319cce1c9fbfc5e999d89a64612`）

改动仅 `Web/src/lib/MSE/h264Player.ts`（+4 行）+ `Web/src/test/h264Player.watchdog.test.ts`（+2 测试）。`watchdog.ts`/`websocket-init.ts` 零改动（纯函数与 worker 侧状态机本就正确，缺陷在主线程接线）。

### 1.1 缺陷 1 — 重连 open 后旧帧时间 → 假 5s stall

- 病灶：`case 'open'` 置 `wsOpenAt`/清 `wsReconnectPending`/`wsGiveUp`，但不清 `lastValidFrameAt`；watchdog 以 `lastValidFrameAtMs > 0` 判 steady，用旧连接末帧起算 5s，而非本轮 `wsOpenAt` 起算 10s join。
- 修复：`case 'open'` 增 `this.lastValidFrameAt = 0` —— 每次成功 open 建立新 join 世代/计时边界。重连后管线本就需新 IDR，按 join 相位处理语义正确；steady 5s 在新帧到达后自然恢复。
- 负例测试（修复前先行编写并确认失败，失败形态 = 病灶本身）：`starts a new join window when a reconnect re-opens; the stale last frame must not fire the 5s steady stall` —— open→帧→reconnect（退避）20s 后 re-open，+6s（>5s 旧帧龄、<10s join）必须零重启（修复前此处即触发 `no-valid-frame`）；+10.5s 才以 `no-valid-frame-join` 恢复。

### 1.2 缺陷 2 — `Connection replaced` 关闭后假 live → 自激重启循环

- 病灶：worker 对 replaced/manual 终止只 post `type:'close'` 并停止重试；主线程 `case 'close'` 只 dispatch `wv_close`，`wsOpenAt` 残留 → `isSocketLive` 误判在线 → 旧末帧 5s `no-valid-frame` hardRestart → 再挤掉同 IP 新连接（与固件 `ws_stream_cleanup_old_connections(client_ip)` 同 IP 替换策略叠加成自激循环，@604 SOAK2 双客户端交替表现吻合）。
- 修复（范围决定：**所有 terminal close**，非仅 replaced）：worker 只在不再重试的终止路径 post `'close'`（manual close / `Connection replaced`；会重试的路径一律走 `'reconnect'`），故每条 `'close'` 都意味着 worker 已停、不再自愈 —— 主线程统一 `wsOpenAt = null; wsReconnectPending = false; wsGiveUp = true`：watchdog 全静默（owner `ws-retry`）、`isSocketLive` 为 false、无自动重启；会话态保留，手动 Retry（`handleReload` → `start()`）重臂全部三个旗标（`start()` 既有逻辑，UI 侧 `wv_close` 仅 toast，无 UI 级自动重启，已核 `player.tsx:141-152`）。
- 负例测试：`marks the socket dead on a terminal close (Connection replaced) and never auto-restarts` —— replaced close 后 120s：零 `stall-recover`、原 worker 未 terminate 未重建（`workerOf(player)` 恒等原引用）、仅 1 次 `connect`、零 `disconnect`，终态 `wsOpenAt=null/wsReconnectPending=false/wsGiveUp=true`。修复前失败于 `stall-recover('no-valid-frame')` + worker 被替换重连（自激循环现场）。

### 1.3 验证

- 看门狗/worker/接线三测试文件 **25/25**（既有 23 + 新增 2）。
- 全量 vitest：**306 passed / 1 failed** —— 唯一失败 `h264Player.test.ts > rejects payloads without an Annex-B start code` 为**预存**：`checkFrameData` 的"非空 payload 兜底"（commit `c89517169`/`c05779154` 引入）早于官方起点，`fc1e91519` 上该测试与兜底代码即已同时存在、同样失败（已用 `git show fc1e91519:...` 核对），与本线改动无关，未顺手改动。
- `tsc --noEmit` PASS；`eslint --quiet`（两改动文件）PASS；`pnpm build` PASS；`lint-staged --diff=HEAD~1..HEAD`（钩子等价）PASS。
- 基线对账：`bba5baf2c` 上全量 304 passed / 同一预存 1 failed —— 本轮 +2 通过、零回归。

---

## 2. 交付身份校正（A comment 6758150393 / 6757957173 要求，仅补证据，未改版本号）

### 2.1 容量两口径分列（口径 = `build/ne301_App.bin` 可加载镜像字节数，即占用 AXISRAM1_2_S 执行区的镜像；区总 4,193,280 B）

| 锚点 | 源 SHA | BUILD | App.bin (B) | 产物状态 |
|---|---|---|---|---|
| 官方起点 | `fc1e91519371bea512ea56cb5a7c09a7e9efacc6` | 603 | **4,129,108** | 盘上可复核（`lean-193-m23/build`，SHA256 `e70ea2b22721a3f76e9f23574c60244d100e100e6cb4b74ba029c30ae080c078`） |
| 中间基线（A 指明已含 M2/M3） | `f2b2051e7686064277cb4c4e47aaf6ddc59c4aa6` | - | **4,129,468** | B §7.14 报告值；该构建产物未在盘保留，本轮未重算（A 允许用现有证据） |
| 最终候选 | `bba5baf2cfd95b826c00f43eeabb90f534041c5c` | 616 | **4,124,540** | 盘上可复核（`lean-193-p1b/build`，SHA256 `1123c9a6e8b30c2a6d5e3f4354303e00a52f1d9de5b63f0cbeb5bdb5507be50f`） |

- **口径 1（相对中间基线 `f2b2051e7`）**：4,129,468 → 4,124,540 = **−4,928 B**。仅代表 M4+后续合并段，**不可称"相对官方起点"**。
- **口径 2（相对官方起点 `fc1e91519`）**：4,129,108 → 4,124,540 = **−4,568 B 净值**。
- 盘上可复核中间读数（供任一口径重算）：BUILD 597=`4,123,996`、610=`4,129,884`、611=`4,129,172`、615=`4,124,036`、616=`4,124,540`。
- 占用率：616 镜像 4,124,540 / 4,193,280 = **98.36%**。

### 2.2 SRAM_POOL 完整账（口径 = ELF 可分配段按地址归属 `0x34000000..0x341b2000`；方法 `arm-none-eabi-size -A -x` + 区间求和，可复现）

| 项 | 值 |
|---|---|
| baseline（官方起点 `fc1e91519`，BUILD 603） | **1,064,064 B**（59.86%） |
| final（`bba5baf2c`，BUILD 616） | **1,063,568 B**（59.83%） |
| delta | **−496 B** |

- 澄清：§7.11/§7.14 出现的 **+32 B** 属相邻构建对（615→616：1,063,536 → 1,063,568）的行内数字，与官方起点口径不可混称。
- 边界说明：`.uncached_bss`（0x2e520 B @ `0x341b2000`）落独立区 `AXI_SRAM_UNCACHED`（`0x341b2000..+0x4e000`），**不计入** SRAM_POOL（链接脚本 Memory Configuration 已核）。

### 2.3 双包身份配对（BUILD 616 候选，2026-11-10 构建，源树 `bba5baf2c`）

**APP 616**

| 项 | 值 |
|---|---|
| 包文件 | `ne301_App_signed_v4.3.1.616_pkg.bin` |
| 大小 / SHA256 | 4,126,592 B / `9fdbb73b116ccc58ee36bf3b836fe76996224359542e35f1e3aa01f1dc322182` |
| 内层签名件 | `ne301_App_signed.bin` 4,125,568 B / `8e4498415dd7dcf5b8f83f632c0f14fbd3923ae22c607d91b018d69e563241b7` |
| 原始镜像 | `ne301_App.bin` 4,124,540 B / `1123c9a6e8b30c2a6d5e3f4354303e00a52f1d9de5b63f0cbeb5bdb5507be50f` |
| 源 SHA | `bba5baf2cfd95b826c00f43eeabb90f534041c5c` |
| 构建参数 | 本地构建 2026-11-10；工具链 Arm GNU 13.3.Rel1（ELF `.comment`）；Makefile 默认 `OPT = -g3`（ELF 含 `.debug_info` 6.5 MB 佐证）；`VERSION_BUILD=616`；固件串 `4.3.1.616` |
| 闪写 | `make flash-app` → App1 `0x70100000`（4 MB） |
| 回退 | App2 OTA 槽 `0x70500000`（4 MB）保留上一 BUILD 签名包，或直接重刷既往签名包；官方起点锚点件：`ne301_App_signed_v4.3.1.603_pkg.bin` 4,131,168 B / `f6560dac8cd913bfc5671018791f8324c93ca91474710261fe46be96e3309c8d`（源 `fc1e91519`） |

**Web 1.5.0.8**

| 项 | 值 |
|---|---|
| 包文件 | `ne301_Web_v1.5.0.8_pkg.bin` |
| 大小 / SHA256 | 306,089 B / `c9a96b7098b1f1abe17c7a0d85e3559044fc04f7688d802c198400eb3b0bdb49` |
| 内层 | `ne301_Web.bin` 305,065 B / `6db81a1a417ae86349d81b32e4b31872d03525fd92c786632a5f2ae95235415d` |
| 源 SHA | `bba5baf2cfd95b826c00f43eeabb90f534041c5c`（含 Web B2 修复资产） |
| 版本串 | `version.mk:76` 固定 `WEB_VERSION_OVERRIDE := 1.5.0.8`，与 BUILD 616 **非**同步增长 |
| 闪写 | `make flash-web` → Web 分区 `0x71900000`（1 MB，Makefile `FLASH_ADDR_WEB`） |
| 回退 | Web 分区无双槽机制 → 回退 = 重刷既往 Web 包；bundle 只能以 SHA256 识别（见下） |

**同版本串 ≠ 同 bundle 的实证（A 的关注点）**：`lean-193-b2` 树（`1c39a5b7f`）与 `lean-193-p1b` 树（`bba5baf2c`）各产出同名 `ne301_Web_v1.5.0.8_pkg.bin`，大小同为 306,089 B，SHA256 不同（`d7d90e0c7da27d57e77ac0c3d6e55fc8e83b2c9daac30ec1f4312aee0774b830` vs `c9a96b7098b1f1abe17c7a0d85e3559044fc04f7688d802c198400eb3b0bdb49`）→ 任何 Web 交付必须以 SHA256 + 源 SHA 配对追溯。

**B3 影响注记**：本轮 Web 源变更（§1 两修复）后正式 PR 前需重产 Web bundle（版本串仍为 1.5.0.8，须以新 SHA256 重新配对）；APP 镜像不受 Web-only 源变更影响 —— 已核 ELF/map：镜像内无嵌入前端资产（`web_assets.c` 仅 16 KB 运行时解析器，运行时解析 Web 分区 `asset.bin`）。

---

## 3. 推送与后续

- 修复 commit：`6c3dc98b60bac319cce1c9fbfc5e999d89a64612`（本证据 commit 随后）。
- 唯一推送：`lean/193-b3` → 远端 `pre-review/193-final-candidate` 一次 fast-forward（基于 `bba5baf2c`，无 force）；新远端 SHA 在 #193 评论中报告。
- 预期 A 复审本 delta（h264Player.ts +4 行 / +2 负例测试 / 本证据文档）后裁定一次性正式 PR。
