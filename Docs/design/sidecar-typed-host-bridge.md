# #84 受信 Sidecar 资源装载与隔离 typed Host bridge — 实现与证据

日期：2026-10-17。本文件描述 Issue #84（统一 Browser Bridge v1 规范下）在 Host 侧的生产实现、安全边界、测试矩阵与 NOT_RUN 清单。权威设计输入：

- #84 comment 6229932486（统一 Browser Bridge v1 冻结；单一协议权威）
- ne30x-app #32 comment 6229937817（App 侧镜像约束；只读取源）
- #84 comment 6185352316（User 批准 Tab 次序：Line Crossing (App) 恒在 MQTT/MQTTS 左侧第一位；可信 ACTIVE 默认进入 App）
- #83 comment 6185136285（#83→#84 唯一受信 ACTIVE descriptor 投影；#84 只消费、不自造身份/世代）
- #79 comment 6185024093（卸载确认/处理中/成功/异常已批线框稿）

## 1. AC1 — 受信资产装载（Custom/Services/Web/web_assets.{c,h} + web_server.c）

- 每次资源请求前 `web_sidecar_projection_refresh()` 重新执行 `app_sidecar_lifecycle_query`（复用 #83 生命周期公开接口，不改其内核），仅当 `state == ACTIVE` 时缓存投影；NONE/STAGED/VERIFIED/UNKNOWN/UNAVAILABLE、native 缺失、trust anchor 未提供一律清空缓存。
- 投影建立时做预算闭包校验：逐文件路径合法性（禁止 `\`、`%`、`:`、控制字符、空段/`.`/`..` 段、长度上限）、`file_len ∈ (0, 1MiB]`、`Σ file_len == asset_payload_len`。
- 资源路由 `/appsite/g<service_generation>/<logical path>`：generation 必须与当前投影完全相等；逻辑路径按规范化后与签名 manifest 文件表**完全相等**（strcmp，无前缀匹配、无 index.html 回退、无 Host 静态根 libs 回退）；storage_path 仅作为内部读取键，绝不出现在 URL/响应中。
- 逐资源每次读取后校验 `len == file_len` 与 SHA-256；不匹配、存储文件缺失、读取失败一律 404（显式拒绝、无回退）。响应固定 `Cache-Control: no-store` + `X-Content-Type-Options: nosniff` + `Cross-Origin-Resource-Policy: cross-origin`——废除 legacy 公共 86400s 304 缓存授旧世代重载的路径。
- 非 PoC 构建（`NE301_APP_HOST_POC` 未定义）整段编译剔除，`/appsite/` 前缀不会落入 legacy 静态回退。

## 2. AC2 — 第一业务槽 UI（Web/src/pages/applicationManagement/ + store）

- `firstSlotFor(view, authBlock, sidecar)` 决策：`not_installed → Deploy`；native installed + Sidecar `active` + `host_ui_abi==1` + app_id 相等 + `app_site_origin` 与 Host admin **跨 origin 且跨 site**（`isCrossSite`：站点=IP 字面量/localhost 自身或最后两段域名）→ `sidecar` 业务槽；native-only / staged / verified / unknown / unavailable / origin 未配置 → 可信 Manage/Status，不创建任何业务 iframe。
- 顶层 Tab 次序恒为 `Line Crossing (App) → MQTT/MQTTS → Webhook`；可信 ACTIVE 且用户未手动切换时默认进入业务 Tab；业务内容只经 `<iframe sandbox="allow-scripts allow-same-origin allow-downloads" src="{appSiteOrigin}{ui_base_path}{entry_path}">` 装载签名同包资源，Host 不重新实现业务 JSX/统计/画线/模拟视频。generation 变化通过 iframe `key` 强制重建。
- 会话失效路径：Sidecar identity/generation 变化、grant 被拒、broker `onRevoke`（含 ACK 超时上限、server `revoked=true`、伪造消息防护）、组件卸载、卸载/更新流程——统一 `broker.revoke()` + 移除 iframe + 拒绝晚到消息（端口关闭后 `post` 抛错即忽略）。
- 卸载交互遵守 #79 线框稿：二次确认（取消/×/Esc 无副作用）→ 处理中（“正在撤销 App 访问权限并清理专属数据”）→ 成功（“已卸载/Line Crossing 及其专属数据已删除”）→ 异常（`cleanup_pending` 状态映射为“卸载尚未完成…不允许将同身份重装视为全新安装”，store 新增 `cleanup_pending` view kind）。卸载前先 `POST /app/sidecar/bridge/revoke {all:true}` 撤销全部 grant。

## 3. AC3 — typed broker（Web/src/services/sidecarBridge.ts + api_app_session_module.c）

协议固定 `protocol:"ne301.sidecar.bridge.v1"`，消息集合 `HELLO/GRANT/REQUEST/RESULT/EVENT/REVOKE`，公共字段 `seq:u32`、`grant_id`(64 hex)、`identity`（publisher/app/key_sha/native_tbs/ui_tbs/version/host_ui_abi/四代际）。限额：消息序列化 ≤16KiB、并发未决 REQUEST ≤4、请求超时默认 10s、连续 2 次超时 REVOKE。

服务端（C，`/api/v1/app/sidecar/*`，全部 high-privilege admin 认证）：

- `GET /app/sidecar/ui`：仅 ACTIVE 返回受信投影（identity、ui_base_path、entry_path、app_site_origin——仅来自部署配置 `app_bridge_app_site_origin_set`，客户端输入不可决定）。
- `POST /app/sidecar/bridge/grant`：ACTIVE 校验 + 能力白名单（12 ops 之外拒绝）+ 4 slot 上限；grant_id 由 CTR_DRBG（熵失败时 SHA-256 链式回退，永不输出全零）生成；绑定 identity/generations/过期(5min)。
- `POST /app/sidecar/bridge`：每次调用重查 descriptor（非 ACTIVE → REVOKE+REJECTED）、identity/generation/会话世代匹配（不匹配 → REVOKE+REJECTED `IDENTITY_OR_GENERATION_CHANGED`）、能力白名单、`(op, request_id)` 去重回放环、载荷 ≤16KiB/512B(JSON 有效性与深度键数由 `app_host_control_json_object_valid` 把关)。
- `POST /app/sidecar/bridge/confirm`：counters.reset 专用一次性管理员确认令牌（120s TTL、单 grant 单 op、恒定时间比较、用后即焚）。
- `POST /app/sidecar/bridge/revoke`：单/全部撤销 + 清确认令牌 + ui_generation 单调递增。

浏览器侧 broker：HELLO 以确切 targetOrigin 发送（绝不 `*`）；GRANT 在端口激活后发送；入站消息校验 protocol/type/seq 严格递增/grant/identity 完全匹配，未知类型、伪造（origin 不符、grant 不符、seq 回退、重放 request_id、超限、超时）一律 fail-closed（REJECTED 或 REVOKE）；`origin=""` 是 Chrome MessagePort 事件的固定语义，窗口级伪造（origin 校验 + 仅信任 HELLO 时转移的唯一 port + grant_id 不可猜测）兜底，`origin=null/任意值` 不构成身份。操作映射（AC4）：

- `snapshot.read` → `app_host_control_snapshot_read`：fresh → APPLIED+payload；stale → UNKNOWN+freshness=stale；无 → UNAVAILABLE；会话世代不匹配 → UNKNOWN。
- `config.read` → control 状态：无活动会话 → UNAVAILABLE `SESSION_NOT_ACTIVE`；有 → APPLIED + config_generation。
- `config.apply` → 会话必须活动；注册 CONFIG_APPLY（期望世代必填）后有界等待最终回执：APPLIED 才 APPLIED（附 result 世代/revision）；STALE → CONFLICT；REJECTED_* → REJECTED；STORAGE_* → UNKNOWN；超时 → UNKNOWN `ACK_TIMEOUT`（不乐观提交）。
- `counters.reset` → 必须持一次性管理员确认令牌，否则 REJECTED `ADMIN_CONFIRM_REQUIRED`；确认后按 CONFIG_APPLY 同样回执语义（DESTRUCTIVE_CONFIRMED 标志）。
- `report-policy.*`、`enable.*`、`live.*`、`video.open/close` → 一律 UNAVAILABLE（`CAPABILITY_NOT_REVIEWED` / `VIDEO_MEDIA_GRANT_UNAVAILABLE`），绝不伪造 APPLIED/成功。

## 4. 测试矩阵与证据

| 套件 | 数量 | 覆盖 |
| --- | --- | --- |
| tests/web_sidecar_assets（新增 C） | 95 checks 0 fail | ACTIVE 投影、精确匹配、路径逃逸/编码/绝对路径/大小写/尾斜杠拒绝、路由 generation 解析、哈希+长度校验、存储缺失拒绝、卸载/换 native/anchor 缺失后拒服、换代后旧 URL 失效 |
| tests/web_sidecar_bridge（新增 C） | 380 checks 0 fail | grant 需 ACTIVE/未知能力拒绝/4 slot 上限/身份绑定；未知 grant、OP_NOT_IN_GRANT、8 项 UNAVAILABLE 映射；snapshot fresh/stale/无；config.read 会话态；config.apply APPLIED/CONFLICT/ACK_TIMEOUT/世代必填/载荷必填；counters.reset 无令牌拒绝+令牌单次+重放拒绝；REPLAY 去重；换代 REVOKE；revoke 全清；ui 投影（origin 非法输入拒绝） |
| Web vitest（新增 sidecarBridge.test.ts 16 + sidecarSlot.test.tsx 15） | 31 | envelope 黄金对象与逐字段变异拒绝、16KiB 上限、握手 targetOrigin、RESULT 回显、能力外拒绝、重放、4 pending 上限、ACK 超时→UNKNOWN→REVOKE、server revoked、危险确认 EVENT、视频三态、跨 site 判定、firstSlot 全矩阵、store 映射（active/unavailable/origin 未配置/查询失败/鉴权）、iframe sandbox/src/授权拒绝不建 frame/卸载撤销、#79 卸载三态 |
| tests/sidecar_host_bridge（新增真实 Chrome E2E，playwright-core + Chrome for Testing 143） | 9 tests 0 fail | 跨 site（127.0.0.1 vs localhost）沙箱 iframe 双 Worker(2/2)+HELLO/GRANT+snapshot APPLIED(fresh)；签名资产 200/no-store、未知/穿越/旧世代/篡改 404；伪造窗口消息被忽略且会话不受扰；request_id 重放 REJECTED；policy-save UNAVAILABLE（无假成功）；counters.reset 无确认拒绝；config.apply hold→UNKNOWN(ACK_TIMEOUT)→解除后 APPLIED；世代切换 REVOKE+旧 frame 销毁+旧世代 404；同 site 拒绝（不建 iframe）；ACK 超时不乐观提交 |

既有回归（0 fail）：app_report 552、policy(=app_host_control) 441+107、admission 179+97+84+61、run 214、session 540、installer 271+280+173+54、state 188+260、sidecar 155、sidecar_lifecycle 351、app_host_validate 21；Web vitest 基线 h264Player 既有 1 失败保持不变。ARM 编译：`make app` 与 `make app APP_HOST_POC=1` 均通过（PoC 关闭时本卡代码整段条件编译剔除）。docs-sync：`python3 Script/gen_web_api_docs.py --check` 通过（app_session 端点参考已再生）。

## 5. #25 runtime adapter BLOCKED / NOT_RUN 边界

- App #25/#32 的生产签名 runtime consumer（ne30x-app 仓内的 `LineCrossingCapabilities` adapter）本卡 NOT_RUN/BLOCKED：tests/sidecar_host_bridge/app/ 是**测试 fixture 消费者**（实现同一 wire 的对端，用于反证 Host 侧契约），不是生产 App 适配器，不构成 AC3 生产完成证据。其 `probe:ready` 端口唤醒是 fixture 引导步骤。
- 真机 LittleFS/RAM/PSRAM 预算、目标板 DNS/TLS/独立 App site 部署、真实摄像头/8081 流/WS/MSE 授权绑定、掉电、生产签名与安装许可：NOT_RUN（#40/#48/#58 现场验证项）。`app_bridge_app_site_origin_set` 的部署接线在设备侧保持 NULL=fail-closed（Manage/Status，无 iframe）。
- FileSaver `saveAs`（#99 FAIL）兼容状态保留：本卡未宣称完整录制导出 PASS；视频能力在获得真实媒体授权前恒为 Unavailable。
- #88 policy、#30 Enable、#91/App #27 live 未受审接入 → 全部 UNAVAILABLE，不从 report window/旧 v3 虚拟能力；Host-owned MQTT/Webhook/backlog Save 未显示任何成功。
