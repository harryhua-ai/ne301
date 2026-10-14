# r1 correction renders — #57 PR #78 (baseline 46481a0f → correction)

本组截图是 **correction 后真实构建产物的浏览器渲染图**（非设计示意图、非 SVG 重绘），
用于关闭 AC2/AC5 阻断中的"可复核视觉证据"。设备实机截图本轮未采集，见下文
`DEVICE_UI_CAPTURE_NOT_RUN`。

## 环境 / 来源

| 项 | 值 |
|----|----|
| 代码来源 | 本仓库 worktree 分支 `agent/57/72e7e206`（correction delta，未含 "A REVIEW" 结论之外的其它改动） |
| 构建产物 | `Web/` `tsc -b && vite build`（`pnpm build`），静态托管于本地 mock host |
| 服务 | `/tmp/57-shots/mock-server.mjs`（上一轮同 worktree 执行器所起，端口 8931，静态服务 `Web/dist` + mock `/api/v1/*` 应答，scenario=business） |
| Mock 说明 | **mock 驱动的是真实构建页面**（同一份 dist，与固件打包流程 `pnpm build:bin` 同源）；mock 只伪造 HTTP API 应答，不伪造布局/组件行为 |
| 采集工具 | playwright-core 1.64.0 + Chromium for Testing 1243（本机缓存），脚本 `capture.cjs`（本地 /tmp，未入库） |
| 视口 | wide = 1440x900 @2x；narrow = 390x844 @2x |
| 语言 | zh（简体中文，设备实际语言）与 en 各一组 |
| 数据 | mock 报文：installed line-crossing-64 v3.1.0、session running、snapshot fresh（累计 IN 11 / OUT 4，窗口 IN 2 / OUT 1，config_generation=7）、report 队列 3 条（succeeded/pending/failed/not_configured 各态） |

## 与 baseline（`people-counting@6fb5dd79` 成熟布局）的对照

业务内容区按 people-counting 业务页布局忠实移植（详见
[people-counting-layout-mapping.md](./people-counting-layout-mapping.md)）：

- 业务 tab 不再装入 `sm:w-xl` 窄卡（`applicationManagement/index.tsx` 旧回退点），
  business 激活时外壳为 `flex h-full flex-col overflow-hidden`，业务内容
  `flex h-full min-h-0 flex-col overflow-hidden p-4` +
  `grid lg:grid-cols-[minmax(0,3fr)_minmax(320px,1fr)]`（`LineCrossingPage.tsx`）；
- 左列 = 主视频/画线（自然纵横比 `w-full h-auto`，ResizeObserver 1:1 像素 viewBox，
  与视频同一坐标空间；MSE 错误有限 hardRestart、冻结 watchdog、断流退避重连、
  IN/OUT 画面叠加、手动刷新）——行为移植自 `peopleCounting/VideoPreview.tsx`，
  数据仍来自 #21 v3 App-owned 快照（不引入旧业务直连）；
- 右列 = 紧凑统计条（累计/窗口 IN-OUT 大数字 + 实时/过期徽标）+ 可滚动配置区
  （实时数据/参数配置/高级设置 + 上报状态 + 保存栏），移植自
  `peopleCounting/index.tsx` 的 `StatsPanel` 上、`ConfigPanel` 下的滚动结构；
- MQTT/MQTTS、Webhook 两个通用 tab 及 #39 四态首位置/通用管理 Dialog 保持原样
  （对比组 `r1-mqtt-*.png`，未重设计）。

与本轮 mock 截图的已知差异（诚实标注）：

1. mock 环境无真实 H.264 WS 流（8081 无服务）：视频区为黑底 + 标线叠加 +
   IN/OUT 叠加；`onLoadedMetadata → aspectRatio 锁定真实流宽高`的行为在 mock 中
   不可见，代码路径与 `VideoPreview.tsx` 一致，待实机验证（归 #48 真机项）。
2. 截图为 mock 数据（fixture 数值见上），非设备实时数据。

## 文件索引

| 文件 | 内容 |
|------|------|
| `r1-home-{zh,en}-{wide,narrow}.png` | 当前首页（未改动页面基准对照） |
| `r1-business-live-{zh,en}-{wide,narrow}.png` | 业务页默认：左视频/画线 + 右统计条 + 实时数据/上报状态 |
| `r1-business-config-{zh,en}-{wide,narrow}.png` | 参数配置区（含草稿基准世代、统计与上报保存栏） |
| `r1-business-advanced-{zh,en}-{wide,narrow}.png` | 高级参数区 |
| `r1-mqtt-{zh,en}-{wide,narrow}.png` | MQTT/MQTTS tab（#39 通用模块未重设计的对照证据） |

宽屏业务页呈现 `3fr + minmax(320px,1fr)` 双列；窄屏（390px）回落单列纵向堆叠
（`grid-cols-1`），与 people-counting 响应式行为一致。

## DEVICE_UI_CAPTURE_NOT_RUN

本轮**未采集用户现役设备实机截图**：执行环境不可达该设备的本地 LAN
（无地址/无只读访问凭据），且评审边界要求涉及 Stream start/stop 副作用的访问须
先行确认"仅取证、不改用户运行设置"，本轮无法确认。因此：

- 不声称任何"真实设备视觉 PASS"；
- 未以设备不可达为由回退到旧 Mock 布局——本组证据为 correction 后真实构建渲染；
- baseline 以 `people-counting@6fb5dd79` 源码布局说明 +
  [people-counting-layout-mapping.md](./people-counting-layout-mapping.md)
  映射/隔离对照形式提交；
- 未上传任何设备口令、密钥、真实视频帧或个人影像（本组全部为 mock 数据渲染）。
- 真机 v3 热控/外网/实机截图仍归 #48。
