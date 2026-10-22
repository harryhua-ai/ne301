# Issue 129 App Status 截图证据

全部截图来自真实运行的 NE301 Web（`pnpm dev`，Chromium headless），仅 `/api/v1/app/*`
设备响应为 Playwright 路由注入的模拟数据（文件名以 `mock-` 前缀标注），不冒充真机
App 状态。视口 1280x800，`*-narrow` 为 375x720 窄屏。

| 文件 | 场景 | 注入的设备状态 |
| --- | --- | --- |
| mock-empty-state-zh.png / -en.png | AC1 中性空状态（中/英） | `state:"none"` |
| mock-empty-state-zh-narrow.png / -en-narrow.png | AC1 窄屏可用性 | `state:"none"` |
| mock-querying-zh.png | 首次查询中 | 请求挂起 |
| mock-undetermined-zh.png / -en.png | 无法确认状态 + 重新查询 | `state:"unknown"` |
| mock-unavailable-en.png | 不可执行原因 + 受控恢复入口 | `status:"unavailable"` |
| mock-cleanup-pending-zh.png | 清理未完成提示 | `state:"cleanup_pending"` |
| mock-401-relogin-en.png | 401 真实路径：清除凭据并跳转登录 | HTTP 401 |
| mock-forbidden-zh.png | 403：全局权限提示 + 面板重查，无身份泄露 | envelope FORBIDDEN |
| mock-installed-no-business-zh.png | 已安装无业务 Tab：管理入口仅在此面板 | `status:"installed"`, ui active |
| mock-manage-dialog-zh.png | 既有单层管理 Dialog 回归 | 同上 |
| mock-deploy-dialog-zh.png / -en-narrow.png | 既有单层部署 Dialog（未确认不上传） | `state:"none"` |
| mock-sidecar-tab-en.png / -sidecar-business-manage-en.png | 受信 Sidecar 业务 Tab + 业务页右上角管理入口 | sidecar projection active（grant 被 mock 拒绝，故显示撤销横幅） |
| mock-panel-isolation-mqtt-en.png | MQTT 面板隔离：无任何管理应用入口 | 同上 |

已知模拟局限：grant/sidecar 撤销横幅、MQTT 表单骨架由无真机后端导致；真机验收归 #48/#115。
