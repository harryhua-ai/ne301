# r2 people-counting inheritance correction — Issue #57 / PR #78

本组是 `c3248062039d2b5d83c393e18878f45d7318df05` 的同 attempt 差量修正，范围仅为已调试 people-counting 业务页的代码继承边界。四个源组件来自 `people-counting@6fb5dd798ef623316971816475be2208d9fbe018`。逐函数/JSX provenance、保留行为、v3 适配及 schema 不兼容项见 [people-counting-inheritance-map.md](./people-counting-inheritance-map.md)。

## 浏览器渲染证据

PNG 是本 worktree `pnpm build` 产物在本地 mock host 的浏览器渲染，不是静态视觉稿。mock 只提供已安装应用、持续会话、可信快照、热配置回执和报告状态数据。页面及交互代码由构建产物提供。

| 文件 | 内容 |
|---|---|
| `r2-business-zh-wide.png` | 中文宽屏：继承的视频/统计/参数业务结构、快照状态和报告队列 |
| `r2-business-en-wide.png` | 英文宽屏业务页 |
| `r2-business-zh-narrow.png` | 中文窄屏纵向回落 |
| `r2-business-config-zh-wide.png` | 中文宽屏配置分组与热配置保存反馈 |

mock 数值是测试夹具，不代表设备实时数据。mock 没有真实 H.264 WS 流，视频画面为黑底并显示配置线及方向覆盖层；自然宽高比取流路径保留在继承代码中，真实流画面仍待设备验证。

## 验收证据索引

- 源代码映射、保留行为与 v3 差异：[people-counting-inheritance-map.md](./people-counting-inheritance-map.md)
- 交互回归：`Web/src/test/appManagement/linecrossing.test.tsx`、`Web/src/test/appManagement/videoPreviewInheritance.test.tsx`
- 页面渲染证据：本目录 `r2-business-*.png`
- #57 已认可的快照单锁视图/世代身份门、四态管理外壳、MQTT/Webhook、权限及既有 API/渲染证据沿用 r1 交付；本 correction 未修改这些路径。

## DEVICE_UI_CAPTURE_NOT_RUN

本轮未采集设备实机画面。截图是浏览器 mock 渲染；没有声称设备实时视频、热配置或外网能力通过。设备截图和运行态核验状态继续记为 `DEVICE_UI_CAPTURE_NOT_RUN`。
