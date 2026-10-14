# Issue #82 — Line Counting Sidecar 离线可行性

日期：2026-10-14
结论级别：离线研究证据；不构成生产安装或实现授权。

## 结论

| 主题 | 结论 | 边界 |
|---|---|---|
| counting 正式 UI 离线构建 | **YES** | 构建入口固定为 `de25a6f1f431a631a37ff1cda87f616e293e8836` 的 `Web/src/pages/applicationManagement/line-counting-module.tsx`；实际组件和 H.264/MSE/Worker 路径进入本地浏览器。 |
| 控制能力隔离 | **YES（模拟 Host）** | 桌面 Chrome 中，跨源 sandbox iframe 通过窄 typed bridge 工作；Host 管理员凭据没有进入 App，伪造来源、错误 Origin、nonce、App build 和越权能力均被拒绝。模拟 Host 不能证明量产 Host 已具备这些策略。 |
| H.264/MSE 桌面路径 | **YES（Chrome 154）** | 50 个合成 H.264 Baseline access unit 经本地 WebSocket、原 H264Player/worker、JMuxer、MediaSource 解码；`videoWidth=320`、`videoHeight=180`、播放时间推进，未使用 canvas mock。 |
| UI 编辑差额 | **NO：端点拖动** | 正式 UI 显示“Drag endpoints to adjust”，但实际组件没有 pointer/mouse 拖动处理器。指针拖动后的 click 会回到“Click to set line end”并清空线段，需再点一次完成线段。画线起点/终点、方向翻转和保存本身可运行。 |
| 断流状态 | **NO：状态提示未反映断流** | 关闭已连接 WebSocket 后本地播放器重连；UI badge 仍显示 Running，页面没有 Reconnecting 标签。永久断流和设备级重连不在本次证明范围。 |
| 32 MiB LittleFS 量产容量 | **UNKNOWN** | 保守共存模型需要约 5,144,098 B LittleFS 文件空间，再留 8 MiB（假定 25%）恢复/GC 余量；设备实测 free bytes、现存数据和文件系统开销均未取得。 |
| 全局 Sidecar 量产可行性 | **UNKNOWN** | 无真机、无 Flash/NVS/LittleFS 写、无 App 安装；生产信任根、Host 加载/回滚、受信 Origin、设备 RAM 和设备 H.264 能力没有验证。 |

这些结果只回答 #79 的架构讨论。离线 fixture PASS 不等于用户接受 UI，也不等于产品完成。

## 可复现实验

工作树基准：`4026625e8f2bbd917f9db2b2979ef7c176752fb8`，分支 `agent/82/7dffa5a1`。运行环境：Node `v22.22.3`、npm `10.9.8`、Google Chrome `154.0.8037.98`、ffmpeg `8.0`。

```sh
cd tests/counting_sidecar
npm ci --offline --ignore-scripts
npm test
```

依赖须预先存在于本机 npm cache；上述安装关闭 npm 网络并跳过 lifecycle scripts。`npm test` 依次构建闭包、运行 binding/line-counting 测试、编译运行既有 v3 C harness、启动真实桌面 Chrome 浏览器探针。Chrome 可执行文件路径目前固定为 `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome`。构建目录和 v3 C harness 临时目录均在系统 temp；fixture 构建只会替换带本 fixture 标记的 temp 目录。

最终运行记录：

- 完整命令输出：[test-all.log](../../tests/counting_sidecar/evidence/test-all.log)
- 浏览器状态、请求、堆样本和安全探针：[browser-automation.json](../../tests/counting_sidecar/evidence/browser-automation.json)
- counting 页面截图：[counting-sidecar.png](../../tests/counting_sidecar/evidence/counting-sidecar.png)
- 源码/依赖/adapter provenance：[source-closure.json](../../tests/counting_sidecar/evidence/source-closure.json)
- 逐文件资源大小：[fixture-metrics.json](../../tests/counting_sidecar/evidence/fixture-metrics.json)
- 合成视频来源和散列：[h264-fixture.json](../../tests/counting_sidecar/evidence/h264-fixture.json)
- 冻结 v3 harness 输出：[native-v3-existing-harness.txt](../../tests/counting_sidecar/evidence/native-v3-existing-harness.txt)

重跑得到的接受条件为：build 成功；binding 与 counting adapter 测试 `12/12`；冻结 v3 C 回归 `61/61`；桌面 Chrome browser test `1/1`；所有浏览器请求仅访问 loopback，外部请求数为 `0`。临时签名 key 每次生成；Ed25519 签名、tar header 时间会改变 archive/hash，因此不能把 tar 文件或完整包 hash 当作确定性发布产物。

## AC1 — 正式 counting UI 离线闭包

### 固定来源与行为基线

源码锚点是 `de25a6f1f431a631a37ff1cda87f616e293e8836`，不是 people-counting 备份。真实入口和下列业务组件由 build script 使用 `git show <sha>:<path>` 提取，不复制当前分支的旧 UI，也不建立替代静态页：

- `Web/src/pages/applicationManagement/line-counting-module.tsx`
- `Web/src/pages/applicationManagement/lineCounting/{VideoPreview,ConfigPage,StatsAndEvents,LineToolbar,AdvancedPage,fields}.tsx`
- `Web/src/services/api/line-counting.ts` 和 `deviceTool.ts`
- UI card/button/input/label/select/switch、Android switch/device detector、utils、styles、英语 locale、H264Player/MSE/WebSocket init/video worker、FileSaver 和 JMuxer

三份同 SHA 行为基线测试的 hash 在 `source-closure.json`：

- `Web/src/test/lineCountingApi.test.ts`：canonical API 路径、UI permille 与 wire normalized 坐标转换、reset 空 JSON body。
- `Web/src/test/lineCountingNavigation.test.tsx`：Line Counting 保持在 Application Management sibling tab，旧 people-counting route redirect。
- `Web/src/test/lineCountingPage.test.tsx`：草稿/保存、target class 确认、数值编辑和范围钳制、实时事件/轨迹、reset 确认、视频 overlay。

上述原始 Web Vitest 文件已固定并审读，**没有在本 fixture 内运行**；fixture 则用真实 counting TSX + 真实桌面浏览器独立覆盖实时页、IN/OUT 事件、配置保存、高级分组和 overlay。此边界避免把被 mock 的原测试或 #80 的旧 Chrome 演示误报成原 UI 的浏览器验证。

### 离线构建差异

fixture 仅在 temp source tree 替换下列环境入口；业务页面、组件、line-counting API 和 H264Player/MSE 文件保持上述 SHA 原文：

1. `services/request.ts` 被替换为本地 typed bridge transport。Counting API 路径映射为 config/status/stats/events/tracks/reset capability；`deviceTool` 的 preview start/stop 走独立 capability。
2. `getWebSocketUrl`/`sleep` 换为 loopback 等价物，H.264 字节仍走原 WebSocket worker、decoder worker、JMuxer 和 MediaSource。浏览器 detector、Lingui 英文文案和 Sonner toast 使用 fixture adapter。
3. Tailwind v4 在抽取 CSS 前加 fixture-only `@source "..";`，以扫描 temp source 闭包。CSS 产物记录在 adapters provenance 中；未改 `Web/src/**`。

原 `line-counting.ts` 的 REST 接口包含 config、status、stats、events、tracks、reset/isResetting；线坐标在 UI permille 与 normalized wire 之间换算。当前 App Host v3 control wire 则定义 `CONFIG_APPLY`、`COUNTERS_RESET`、`RECEIPT`、`SNAPSHOT` 四种消息（32-byte header、512-byte control message、2048-byte snapshot；见 [app_host_control.h](../../Custom/Services/AppHost/app_host_control.h)）。现有 v3 不是完整 counting REST API 或视频服务。

面向真实 App v3 至少还需定型：逐 App 的 config/status/stats/events/tracks capability 及 JSON schema/freshness；config 与 snapshot 在 512/2048-byte 限额下的序列化边界；reset 与 UI 确认语义；preview lifecycle；独立授权的 H.264/WebSocket 数据路径、重连和取消语义。当前 fixture 的 9 个 capability 名称和 test bridge 都不是生产 wire 格式。

### 功能实测矩阵

| UI 行为 | 结果 | 证据/边界 |
|---|---|---|
| 实时页、runtime badge、IN 与 OUT event | **YES** | browser report 的 `ui.eventDirections`、`runtimeBadge`。事件与 Host 状态是本地模拟值。 |
| 画线起点 → 终点 → draft 阶段 | **YES** | `drawPhaseOne`、`drawPhaseTwo`；阶段由 counting 页面原有 handler 更新。输入事件在 Chrome DOM 内合成，非人工设备输入。 |
| 方向翻转与 normalized 坐标保存 | **YES** | SVG arrow 几何翻转；typed host 收到 0–1 坐标。 |
| 显示为可拖端点并实际拖动 | **NO** | `VideoPreview.tsx` 只挂 `onClick={handleStageClick}`；SVG overlay 为 `pointer-events-none`；无 pointer/mouse drag handler。浏览器探针显示尝试后 phase 回到设置终点，线需重画。 |
| config 页草稿/保存 | **YES** | counter name 写入 local Host bridge；不调用生产 API。原源码使用 `lineCounting.setConfig`。 |
| advanced 分组 | **YES** | 4 个 reporting switch 与 window/backlog 字段出现在真实 `AdvancedPage`。 |
| 视频自然比例 | **YES** | 320×180 intrinsic 与 CSS 16:9 同比，`object-fit: contain`。 |
| 短暂 WebSocket close 后 UI 断流状态 | **NO（提示）** | server 关闭旧 socket 后有新 socket 重连，播放恢复；badge 仍 Running，reconnecting 文案数为 0。源中的 `connState` 固定初始化为 `ok` 且不更新。长期 server outage 未测。 |

## AC2 — 桌面浏览器隔离与攻击反例

Chrome 154 headed、1365×900 viewport、三个仅绑定 `127.0.0.1`/`localhost` 的测试 server。Host origin `http://127.0.0.1:<port>`，App origin `http://localhost:<port>`，WS 为 `ws://localhost:<port>/stream`；Origin 不同、传输仍为明文 HTTP/WS。

| 探针 | 结果 | 实际证据 |
|---|---|---|
| iframe sandbox | **YES** | `allow-scripts allow-same-origin`，没有 `allow-top-navigation`。跨源 parent DOM 读取得 `SecurityError`；top navigation 被阻断，顶层 URL 不变。 |
| Host admin 不下放 | **YES（模拟）** | Host principal `isAdministrator=true`；App principal `false`；App `document.cookie` 空；Host-only `host_admin_session` 与 Authorization header 均未发送到 App。 |
| typed bridge allowlist | **YES（模拟）** | 仅 9 个 counting/preview capability；`host.admin.reboot` 得 `CAPABILITY_DENIED`；越界 config 得 `PAYLOAD_INVALID`。 |
| 消息来源与绑定 | **YES（模拟）** | 同源 sibling 的错误 source、foreign origin、旧 nonce、重绑定后 generation nonce、切换到不同 App build 均拒绝。 |
| 第三方 frame | **YES（模拟）** | CSP `frame-ancestors` 指向精确 Host origin，foreign origin embed 被 Chrome 拒绝。 |
| 旧缓存导航 | **YES（模拟）** | Service Worker 故意返回 `stale-app-build-id` 文档；Host 仍按 build id 拒绝。虽有 `Cache-Control: no-store`，此反例验证 Host 不把缓存响应当可信版本。 |
| 真 H.264/MSE | **YES（Chrome only）** | `MediaSource.isTypeSupported` true、`canPlayType=probably`、video 320×180、readyState 4、播放时间推进；2 个 Worker，50 个 H.264 AUs，mock canvas false。 |
| HTTP/TLS 信任 | **NO：未建立传输信任** | Chrome 对 localhost 报 `secureContext=true`，这是浏览器 localhost 例外；响应仍为 HTTP，视频仍为 `ws://`，没有 TLS、生产证书或设备认证。该结果不得外推到设备/LAN。 |
| 设备/其他浏览器媒体边界 | **UNKNOWN** | 未在板端、Safari/Firefox/Android WebView、真实传感器、真实 H.264 camera source 测试。 |

所有路线都在本机 loopback，浏览器拦截任何非 loopback HTTP request；本次记录外部 request count `0`。合成视频通过原播放链路，不证明摄像头编码参数、长时间抖动或设备 WebSocket 认证可行。

## AC3 — 资源测量与 32 MiB LittleFS 模型

### 桌面 fixture 实测

最新完整重跑的文件数为 7，输出 raw 总量 417,036 B，串接 assets 后 `gzip -n -9` 为 109,836 B。该次测试 tar.gz sidecar proxy 为 112,627 B；fixture manifest 1,193 B、signature 64 B；展开静态包连同 manifest/signature 418,293 B。该次压缩 archive + 展开树的 staging peak sample 为 530,920 B。tar 是测量代理，不是 NE301 的 sidecar 包格式；较早已保存的重跑令 archive/staging 范围为 112,627–112,648 B / 530,920–530,941 B，签名与归档 metadata 会引入变化。

| 构建文件 | bytes |
|---|---:|
| JS main | 260,143 |
| CSS | 45,942 |
| JMuxer | 100,844 |
| FileSaver | 6,954 |
| WebSocket init | 2,000 |
| video worker | 647 |
| index.html | 506 |

Chrome CDP JS heap samples：mounted 4,374,448 B used / 5,406,720 B total；H.264 path 4,488,040 / 5,668,864 B；post-rebind 4,973,156 / 6,979,584 B。`performance.memory` 在媒体路径为 4,903,963 B used / 7,878,183 B total。它们是桌面 Chrome 样本；不含完整 video decoder/GPU/native memory，也不代表板端 RAM 或 PSRAM 余量。板端 RAM 为 **UNKNOWN**。

### 保守 LittleFS 同存预算（模型，不是设备测量）

工作树 [mem_map.h](../../Custom/Common/Inc/mem_map.h) 的 `BOARD_FLASH_SIZE != 128` 路径把 LittleFS 配为 32 MiB，即 33,554,432 B；128 MiB flash 路径是 96 MiB。本模型只按题定 32 MiB 板型计算。Native App package 上限 2,097,432 B（[app_admission.h](../../Custom/Services/AppHost/app_admission.h)）；installer free-space gate 要求 `free_bytes >= package_len × 2`（[app_installer.c](../../Custom/Services/AppHost/app_installer.c)）。

| 阶段/项目 | 模型字节 | 分类 |
|---|---:|---|
| 当前 Native App + 更新候选 Native App，各按最大包 | 4,194,864 | 读代码上限的保守双版本共存；含恢复旧 App 的包空间模型 |
| 当前 + 候选 UI，各按实测展开闭包 | 836,586 | 两个 418,293 B fixture expanded bundle |
| 候选 UI 压缩 staging archive | 112,648 | 本机代理最大实测样本；非真实固件包格式 |
| LittleFS 模拟占用小计 | **5,144,098** | 前三项相加；不含 LittleFS block/directory overhead 和已有用户文件 |
| 恢复/GC 余量假设：32 MiB 的 25% | 8,388,608 | 本调查预算策略，不是 firmware 常量或实测 free bytes |
| 暂定 LittleFS 最低空闲条件 | **13,532,706** | 小计 + 余量；之后还须扣除设备已有内容和文件系统开销 |
| 32 MiB 算术余量 | **20,021,726** | 仅在已有文件与 FS overhead 尚未占用时成立 |
| App install record + pending raw NVS payload | 608 | record 296 B + pending 312 B（[app_installer.h](../../Custom/Services/AppHost/app_installer.h)）；在 NVS，不计入 LittleFS；KV/擦写块开销未知 |

单 App install gate 在最大包长时本身需要至少 4,194,864 B free；因此上述 sidecar 模型与既有 App 更新不能靠名义 32 MiB 容量直接判 PASS。设备实际 free bytes、既有模型/用户数据、LittleFS block overhead、NVS 实占、并发暂存方式均为 **UNKNOWN / NOT_RUN**。WEB partition 的 1 MiB 是独立分区，不在上述 LittleFS 总数中；没有测量其既有 Web UI 占用，也没有证明 sidecar 会安装到该分区。

## #81 / #80 材料复用与复核

PR #81 source commit 固定为 `f819e1e9a40ef5944cf2f52efe22f6919866c5e4`。`source-closure.json` 记录该提交下每个路径的 hash/bytes。逐字节重用 binding verifier/test 与 native v3 harness；line-counting binding 另增 2 个 identity/domain 测试。#81 的 browser demonstration 和 people-counting UI 没有作为 counting browser 通过证据；本实验单独打包 counting 固定 SHA、真实 UI 闭包和新的 real-H.264 test source。桥接 adapter 按 counting REST/API 和独立 video transport 重写/适配，未沿用 #80 UI mock data。

拒绝回归复核结果：完整签名/envelope negative tests 12/12 PASS；冻结 v3 C admission suite 61/61 PASS。测试 trust root/key 每次临时生成；没有将生产 anchor 改为 test key。旧 `neapp-v3` 域名在 sidecar binding negative test 中拒绝；当前生产格式未增加 sidecar wire/domain。

## 反例、未知项和下一次架构决策

已复现反例：改动静态 asset 后 hash mismatch、manifest/signature 改动、unlisted file、路径 traversal/absolute/encoded/backslash、重复资产/sidecar 名、错误原生包/身份、未知 signing key、旧 v3 域、升级后旧 binding、卸载后 dangling sidecar、无权限 bridge、恶意 payload、伪造消息源/Origin/nonce、旧 App generation、错误 build、Service Worker 缓存的陈旧文档。

仍未知：真实设备可用 LittleFS/NVS bytes；实际 App 包与 sidecar 的物理存储布局/安装顺序/掉电恢复；量产侧信任根与证书；生产 Host loader 和原子回滚；设备 Origin、TLS/HTTP、WebSocket 认证和 CSP；目标浏览器 H.264/MSE/Worker；真实 camera bitrate/frame header；持续断流 UI；板端 JS/decoder/RAM/PSRAM 峰值；原始 Web Vitest 是否在当前 workspace package graph 可运行。

**给 #79 的推荐决策：条件性继续 Sidecar 架构设计，不授权生产实现。** 设计边界固定为：Line Counting 是独立来源、无 Host 管理员 cookie/token 的跨源 UI；所有控制面只能用 Host 按 App/build/native identity 绑定的逐 capability bridge；视频帧另走有单独授权、生命周期和断流状态的 data channel。继续设计前，#79 应明确端点编辑需求（修复真实 UI 缺失或改变交互契约），并要求 32 MiB 板型报告实测 free bytes 和 H.264/MSE/RAM 记录。若 #79 当前必须决定是否量产开工，则结论是 **NO：证据不足**。本 Issue 的调查 YES 不等于 #79 DONE。
