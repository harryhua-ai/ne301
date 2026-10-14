# people-counting 源布局 → #57 业务组件映射与隔离对照

基准源：`people-counting` 分支 commit `6fb5dd798ef623316971816475be2208d9fbe018`
（User 实际调好的业务页，只读取证）。本表逐文件说明 correction delta 移植了什么、
适配了什么、明确不移植什么。

## 1. 布局结构映射

| people-counting 源（6fb5dd79） | #57 correction 后 | 移植方式 |
|---|---|---|
| `pages/peopleCounting/index.tsx` 外壳 `flex flex-col h-full p-4 overflow-hidden` | `linecrossing/LineCrossingPage.tsx` 根节点同款类（`flex h-full min-h-0 flex-col overflow-hidden p-4`） | 忠实移植 |
| `grid grid-cols-1 lg:grid-cols-[minmax(0,3fr)_minmax(320px,1fr)] gap-3 min-h-0` | 同款网格（左视频 3fr / 右列 minmax(320px,1fr)），窄屏 `grid-cols-1` 回落 | 忠实移植 |
| 左列 `Card` + `CardContent flex-1 min-h-0 flex flex-col p-0` 包 `VideoPreview` | 同款 Card/CardContent 包 `VideoLineCanvas` | 忠实移植 |
| 右列 `shrink-0` 紧凑统计条（StatsPanel 顶部大数字区）+ `flex-1 min-h-0 overflow-y-auto pr-1` 滚动配置区 | `lc-stats-strip`（累计/窗口 IN-OUT `StatBig` + 实时/过期徽标）+ 同款滚动容器（实时数据/参数配置/高级设置 tab 内容 + 上报状态 + 保存栏） | 结构移植；数据源换为 #21 v3 快照 |
| `StatsPanel.tsx` `StatBig`（大数字+小标签，绿/红着色） | `LineCrossingPage.tsx` 内 `StatBig`（同布局语义，灰态表示无快照 `—`） | 移植；不直连 `peopleCounting.getStats()`，改用 `useAppSessionStore.snapshotPayload` |
| `ConfigPanel.tsx` 分组参数 + 画线按钮（edit/flip/reset） | 保留 #57 已接受的 参数配置/高级设置 tab 结构；画线按钮留在视频组件底部（与视频交互耦合），交换方向=flip 对应 | 部分移植；不重设计已接受的控制流 |
| 视频 `block w-full h-auto` + `style aspectRatio`（metadata 锁定） | `VideoLineCanvas.tsx` 同款（默认 16/9，`onLoadedMetadata` 锁定真实宽高） | 忠实移植 |
| 舞台 `stageRef` + 点击 → permille（`getBoundingClientRect` 归一化） | 同款点击归一化（绘制模式两点击成线） | 忠实移植（坐标空间=视频渲染盒，标线一一对应） |
| SVG 叠加 `viewBox 0 0 w h`（ResizeObserver 1:1 像素，无畸变）+ 三层辉光线 + 方向箭头 + 端点环 | 同款 SVG 叠加（保留 #57 既有配色 #ffe569/#f86f12 与外点/虚线语义），ResizeObserver 驱动 | 移植 + 保留既有视觉标识 |
| MSE 恢复：`mseError → hardRestart`（≤3 次限次） | 同款限次恢复（`MAX_HARD_RESTARTS=3`，手动刷新重置计数） | 忠实移植 |
| 冻结 watchdog：1s `currentTime` 比较 → hardRestart | 同款 | 忠实移植 |
| 断流重连：`wv_close`/`wv_open` 退避序列 [300ms..15s] + 重连中横幅(重试计数) | 同款 | 忠实移植 |
| IN/OUT 画面叠加（左下，绿 IN/红 OUT，窗口(累计)） | 同款位置与语义；数据=快照 `counters`/`cumulative`；绘制模式隐藏 | 移植 + 数据源替换 |
| 手动刷新（右上 RefreshCw，`resetStartState().start()` + start stream 请求） | 同款（`lc-video-refresh`） | 忠实移植 |
| 编辑阶段提示（底部横幅 hint_click_l1/l2/done） | 既有 `draw_hint_first/second` 文案置于同位置（底部横幅 + 按钮行） | 语义对应 |

## 2. 替换掉的回退实现

| 回退点（46481a0f） | 修正后 |
|---|---|
| `applicationManagement/index.tsx` 业务 tab 装入 `sm:w-xl` 窄卡 | business 激活时全宽外壳（`flex h-full w-full flex-col overflow-hidden`），MQTT/Webhook/通知等非业务路径维持原窄卡 |
| `LineCrossingPage.tsx` 视频/统计/参数整页纵向堆叠 | people-counting 双列网格（宽屏）/单列（窄屏） |
| `VideoLineCanvas.tsx` 固定 `aspectRatio:'16/9'` + `object-cover`（拉伸裁切、坐标错位） | 自然纵横比 + 1:1 像素 viewBox 同坐标空间（`object-cover` 移除） |
| 无卡帧 watchdog / 无断流重连 / 无 MSE 恢复 / 无手动刷新 | watchdog + 退避重连 + 限次 hardRestart + 手动刷新 |
| 计数区 `grid sm:grid-cols-2` 平铺 StatRow（数字不突出） | 紧凑统计条 StatBig（对应 StatsPanel）+ 详情 StatRow 保留在实时数据 tab |

## 3. 隔离对照（明确不引入）

- **不移植旧业务直连**：不引入 `services/api/peopleCounting`（getConfig/getStats/resetTotals
  直连端点）；配置写入走 #56 v3 热控（`/app/control/config` + 回执 watch），
  计数/状态读数走 #21 v3 App-owned 快照（`/app/business/snapshot`），
  上报状态走 #75 报告（`/app/report/status`）。
- **视频通道例外说明**：`VideoLineCanvas` 保留 `deviceTool.startVideoStreamReq/stopVideoStreamReq`
  + WS `/stream` 预览通道——与 people-counting `VideoPreview` 相同的公共预览机制
  （卸载即 stop，先前 candidate 已接受，非业务数据直连）。
- **不动其它页面**：Home、MQTT/MQTTS、Webhook、网络等页面零改动（`r1-mqtt-*.png`
  与 `r1-home-*.png` 为对照证据）；不把业务页改成单列诊断表——上报状态仅为右列
  滚动区内的一个分组。
- **静态 SVG 基准（92b2d64e）**：仅作为 #39 通用入口/中英弹窗的补充基准保留；
  与 people-counting 业务区冲突时以本映射（真实使用参考）为准。
- **AC/权限无改动**：未改 Task Contract AC、未改签名/ABI/高权限门；无需为此修正
  改动任何后端接口形状（读 API 行为修正见 PR 说明的 AC4/AC5 部分）。

## 4. 复核方式

```bash
git fetch origin 6fb5dd798ef623316971816475be2208d9fbe018
git show 6fb5dd79:Web/src/pages/peopleCounting/index.tsx
git show 6fb5dd79:Web/src/pages/peopleCounting/VideoPreview.tsx
# 对照本分支：
git show HEAD:Web/src/pages/applicationManagement/linecrossing/LineCrossingPage.tsx
git show HEAD:Web/src/pages/applicationManagement/linecrossing/VideoLineCanvas.tsx
```
