# #84 受信 Sidecar 资源装载与隔离 typed Host bridge — 实现与证据

日期：2026-10-17（r2 同 attempt 差量修正版）。本文件描述 Issue #84（统一 Browser Bridge v1 规范下）在 Host 侧的生产实现、安全边界、测试矩阵与 NOT_RUN 清单。权威设计输入：

- #84 comment 6229932486（统一 Browser Bridge v1 冻结；单一协议权威）
- ne30x-app #32 已验收 exact main `1f48bdd5a78e789e2a2bd62a3ce460eda9292a10` 的 `apps/line-crossing/ui/src/runtime/bridgeProtocol.ts`（跨仓可执行 golden vector；validateIdentity 恰好 10 字段含 build_id+数值 generation、GRANT 顶层 expires_at_ms+ops、RESULT 严格 9 键）
- #84 comment 6185352316（User 批准 Tab 次序：Line Crossing (App) 恒在 MQTT/MQTTS 左侧第一位；可信 ACTIVE 默认进入 App）
- #83 comment 6185136285（#83→#84 唯一受信 ACTIVE descriptor 投影；#84 只消费、不自造身份/世代）
- #79 comment 6185024093（卸载确认/处理中/成功/异常已批线框稿）
- #99 FileSaver/anchor 证据（生产下载能力保留到独立 ACTIVE signed App-site + Host-scoped grant/user-action gate；本卡 sandbox 默认无 allow-downloads）

## 1. AC1 — 受信资产装载（Custom/Services/Web/web_assets.{c,h} + web_server.c）

（r2 未改动，沿用上轮已认可结论）

- 每次资源请求前 `web_sidecar_projection_refresh()` 重新执行 `app_sidecar_lifecycle_query`（复用 #83 生命周期公开接口，不改其内核），仅当 `state == ACTIVE` 时缓存投影；NONE/STAGED/VERIFIED/UNKNOWN/UNAVAILABLE、native 缺失、trust anchor 未提供一律清空缓存。
- 投影建立时做预算闭包校验：逐文件路径合法性（禁止 `\`、`%`、`:`、控制字符、空段/`.`/`..` 段、长度上限）、`file_len ∈ (0, 1MiB]`、`Σ file_len == asset_payload_len`。
- 资源路由 `/appsite/g<service_generation>/<logical path>`：generation 必须与当前投影完全相等；逻辑路径按规范化后与签名 manifest 文件表**完全相等**（strcmp，无前缀匹配、无 index.html 回退、无 Host 静态根 libs 回退）；storage_path 仅作为内部读取键，绝不出现在 URL/响应中。
- 逐资源每次读取后校验 `len == file_len` 与 SHA-256；不匹配、存储文件缺失、读取失败一律 404（显式拒绝、无回退）。响应固定 `Cache-Control: no-store` + `X-Content-Type-Options: nosniff` + `Cross-Origin-Resource-Policy: cross-origin`——废除 legacy 公共 86400s 304 缓存授旧世代重载的路径。
- 非 PoC 构建（`NE301_APP_HOST_POC` 未定义）整段编译剔除，`/appsite/` 前缀不会落入 legacy 静态回退。

## 2. AC2 — 第一业务槽 UI（Web/src/pages/applicationManagement/ + store）

（r2 仅将 iframe sandbox 收紧为默认安全；槽位决策、Manage/Status、teardown/卸载状态面不变，沿用上轮已认可结论）

- `firstSlotFor(view, authBlock, sidecar)` 决策：`not_installed → Deploy`；native installed + Sidecar `active` + `host_ui_abi==1` + app_id 相等 + `app_site_origin` 与 Host admin **跨 origin 且跨 site**（`isCrossSite`：站点=IP 字面量/localhost 自身或最后两段域名）→ `sidecar` 业务槽；native-only / staged / verified / unknown / unavailable / origin 未配置 → 可信 Manage/Status，不创建任何业务 iframe。
- 顶层 Tab 次序恒为 `Line Crossing (App) → MQTT/MQTTS → Webhook`；可信 ACTIVE 且用户未手动切换时默认进入业务 Tab；业务内容只经 `<iframe sandbox="allow-scripts allow-same-origin" src="{appSiteOrigin}{ui_base_path}{entry_path}">` 装载签名同包资源——**默认无 `allow-downloads`**：video.open/close 恒 UNAVAILABLE、无已授权用户导出动作/可撤销下载令牌之前，下载能力保持关闭（#99 FileSaver signed saveAs 已知 FAIL 保留；完整录制导出 NOT_RUN，待独立授权 gate 后才允许带 allow-downloads 的受限路径）。Host 不重新实现业务 JSX/统计/画线/模拟视频。generation 变化通过 iframe `key` 强制重建。
- 会话失效路径：Sidecar identity/generation 变化、grant 被拒、broker `onRevoke`（含 ACK 超时上限、server `revoked=true`、伪造消息防护）、组件卸载、卸载/更新流程——统一 `broker.revoke()` + 移除 iframe + 拒绝晚到消息（端口关闭后 `post` 抛错即忽略）。
- 卸载交互遵守 #79 线框稿：二次确认（取消/×/Esc 无副作用）→ 处理中（“正在撤销 App 访问权限并清理专属数据”）→ 成功（“已卸载/Line Crossing 及其专属数据已删除”）→ 异常（`cleanup_pending` 状态映射为“卸载尚未完成…不允许将同身份重装视为全新安装”，store 新增 `cleanup_pending` view kind）。卸载前先 `POST /app/sidecar/bridge/revoke {all:true}` 撤销全部 grant。

## 3. AC3 — typed broker（Web/src/services/sidecarBridge.ts + api_app_session_module.c）

协议固定 `protocol:"ne301.sidecar.bridge.v1"`，消息集合 `HELLO/GRANT/REQUEST/RESULT/EVENT/REVOKE`，公共字段 `seq:u32`、`grant_id`（64 hex，匹配 App 侧 `[A-Za-z0-9_-]{16,128}`）、`identity`（**与已验收 App #32 validateIdentity 完全一致的恰好 10 字段**：`publisher_id/app_id/publisher_key_sha256/native_tbs_sha256/ui_tbs_sha256/build_id/host_boot_generation/native_session_generation/ui_generation/sidecar_service_generation`；build_id 为服务端从受信 descriptor 版本合成的有界字符串，sidecar_service_generation 为数值，无 version_major/minor/patch、无 host_ui_abi）。wire envelope 均为精确键集：HELLO 7 键（含 channel）、GRANT 7 键（**顶层 `expires_at_ms` + `ops`**，非嵌套 grant/capabilities）、REQUEST 8 键（`payload` 必为对象；`expected_config_generation` 携带于 payload 内、由 broker 提升到 HTTP body）、RESULT **严格 9 键**（`reason/freshness/config_generation/state_revision/age_ms` 一律收敛进 `payload`：server 提供数据 payload 时原样透传保持 App 侧 exactKeys 校验，失败/诊断时 payload={reason,...}）、EVENT 6 键（payload 严格 `{stream,stream_seq,status?,data?}`；dangerous.confirm 以 `stream:"dangerous.confirm"` 携带 data）、REVOKE 6 键。限额：消息序列化 ≤16KiB、并发未决 REQUEST ≤4、请求超时默认 10s、连续 2 次超时 REVOKE。

服务端（C，`/api/v1/app/sidecar/*`，全部 high-privilege admin 认证）：

- `GET /app/sidecar/ui`：仅 ACTIVE 返回受信投影（管理面 identity 仍含 version/host_ui_abi 与字符串 generation 供 UI 展示，非 bridge wire；ui_base_path、entry_path、app_site_origin——仅来自部署配置 `app_bridge_app_site_origin_set`，客户端输入不可决定）。
- `POST /app/sidecar/bridge/grant`：ACTIVE 校验 + 能力白名单（12 ops 之外拒绝）+ 4 slot 上限；**grant_id/host_boot_generation 全部源自 CTR_DRBG（entropy 源在 ARM 上为 HAL 硬件 TRNG `hrng`、在宿主测试构建上为 /dev/urandom，经 `mbedtls_entropy_add_source` 注册为 STRONG 源）；seed/generate 任一失败 → 显式 `503 SERVICE_UNAVAILABLE`、零 grant/token 签发、槽位不占用、DRBG 进入粘滞 fail-closed（直到 bridge 状态复位），无任何时间戳/指针/hash 回退**；过期用 wall-clock epoch ms（`epoch_ms` 后端钩子，ARM 默认 `time(NULL)`），与 App 侧 `Date.now()` 同一时钟域；绑定 identity/generations/过期(5min)。
- `POST /app/sidecar/bridge`：每次调用重查 descriptor（非 ACTIVE → REVOKE+REJECTED）、identity/generation/会话世代匹配（不匹配 → REVOKE+REJECTED `IDENTITY_OR_GENERATION_CHANGED`）、能力白名单、`(op, request_id)` 去重回放环、载荷 ≤16KiB（JSON 有效性与深度键数由 `app_host_control_json_object_valid` 把关）。写操作要求 payload 内 `session_generation` 与 grant 绑定的当前会话世代一致（缺失 `SESSION_GENERATION_REQUIRED`/不一致 `STALE_SESSION_GENERATION`）；`config.apply` 要求 body 内 `expected_config_generation`（broker 自 payload 提升）与 `payload.candidate`（仅 candidate 下发给 native 控制面，会话/期望世代字段为 bridge 信封不透传）；`counters.reset` 按已验收 App 请求形状不要求 config generation（无显式世代时由 Host 以当前 known generation（缺省为会话世代）满足控制面非零校验），必须持一次性管理员确认令牌。
- `POST /app/sidecar/bridge/confirm`：counters.reset 专用一次性管理员确认令牌（120s TTL、单 grant 单 op、恒定时间比较、用后即焚；令牌同为 DRBG 生成，RNG 失败 → 503 零签发）。App 的 counters.reset REQUEST 不携带令牌；令牌仅走管理员 HTTP 通道并在服务端按 grant+op 消费。
- `POST /app/sidecar/bridge/revoke`：单/全部撤销 + 清确认令牌 + ui_generation 单调递增。

浏览器侧 broker：HELLO 以确切 targetOrigin 发送（绝不 `*`）；GRANT 在端口激活后发送；入站消息按 App golden schema 校验 protocol/type/seq 严格递增/grant（App 正则）/identity 恰好 10 字段完全匹配，未知类型、伪造（origin 不符、grant 不符、seq 回退、重放 request_id、超限、超时）一律 fail-closed（REJECTED 或 REVOKE）；`origin=""` 是 Chrome MessagePort 事件的固定语义，窗口级伪造（origin 校验 + 仅信任 HELLO 时转移的唯一 port + grant_id 不可猜测）兜底，`origin=null/任意值` 不构成身份。broker 不向 App 转发任何管理令牌/管理员 API。操作映射（AC4）：

- `snapshot.read` → `app_host_control_snapshot_read`：fresh → APPLIED+payload（App golden snapshot schema 原样透传）；stale → UNKNOWN；无 → UNAVAILABLE；会话世代不匹配 → UNKNOWN。
- `config.read` → control 状态：无活动会话 → UNAVAILABLE `SESSION_NOT_ACTIVE`；有 → APPLIED + config_generation。
- `config.apply` → 会话世代绑定 + 期望世代 + candidate 必填；注册 CONFIG_APPLY 后有界等待最终回执：APPLIED 才 APPLIED（附 result 世代/revision）；STALE → CONFLICT；REJECTED_* → REJECTED；STORAGE_* → UNKNOWN；超时 → UNKNOWN `ACK_TIMEOUT`（不乐观提交）。
- `counters.reset` → payload 携带当前 session_generation；必须持管理员确认令牌（服务端消费），否则 REJECTED `ADMIN_CONFIRM_REQUIRED`；确认后按 CONFIG_APPLY 同样回执语义（DESTRUCTIVE_CONFIRMED 标志）。
- `report-policy.*`、`enable.*`、`live.*`、`video.open/close` → 一律 UNAVAILABLE（`CAPABILITY_NOT_REVIEWED` / `VIDEO_MEDIA_GRANT_UNAVAILABLE`），绝不伪造 APPLIED/成功。

## 4. 测试矩阵与证据

| 套件 | 数量 | 覆盖 |
| --- | --- | --- |
| tests/web_sidecar_assets（C） | 95 checks 0 fail | ACTIVE 投影、精确匹配、路径逃逸/编码/绝对路径/大小写/尾斜杠拒绝、路由 generation 解析、哈希+长度校验、存储缺失拒绝、卸载/换 native/anchor 缺失后拒服、换代后旧 URL 失效 |
| tests/web_sidecar_bridge（C，r2 更新） | 534 checks 0 fail | grant 需 ACTIVE/未知能力拒绝/4 slot 上限/identity 恰好 10 字段精确键断言（build_id="1.4.2"、数值 service generation、epoch 过期=发放+300000ms）；**RNG seed/generate 失败注入 → 503 零签发、粘滞 fail-closed、清除+复位后恢复、grant_id 全局唯一、confirm 令牌 RNG 失败 → 503**；未知 grant、OP_NOT_IN_GRANT、8 项 UNAVAILABLE 映射；snapshot fresh/无；config.read 会话态；config.apply APPLIED/CONFLICT/ACK_TIMEOUT/世代必填/candidate 必填/载荷必填/session 世代绑定（缺失+不一致）；counters.reset App 形状（仅 session_generation）无令牌拒绝+令牌单次+重放拒绝+世代世代不一致拒绝；REPLAY 去重；换代 REVOKE；revoke 全清；ui 投影（origin 非法输入拒绝） |
| Web vitest（sidecarBridge.test.ts 19 + sidecarSlot.test.tsx 17） | 36（全套其余 124 既有基线不变） | envelope 黄金对象与逐字段变异拒绝（含 13 字段 legacy identity 拒绝/恰好 10 字段接受/多余字段拒绝）、16KiB 上限、严格 7 键 HELLO、严格 7 键 GRANT（顶层 expires_at_ms+ops）、严格 9 键 RESULT（payload 收敛 reason；数据 payload 原样透传保 exactKeys）、能力外拒绝、重放、4 pending 上限、ACK 超时→UNKNOWN→REVOKE、server revoked、dangerous.confirm 严格 EVENT（stream/stream_seq/data）、视频三态、跨 site 判定、firstSlot 全矩阵、store 映射、iframe sandbox=allow-scripts allow-same-origin（无 allow-downloads）/src/grant 无 identity 不建 frame/授权拒绝不建 frame/卸载撤销、#79 卸载三态 |
| tests/sidecar_host_bridge（真实 Chrome E2E，playwright-core + Chrome for Testing，**App 侧为已验收 ne30x-app #32 exact main `1f48bdd5` 重建并 d=1 dev key 重签名的 NEUI 生产 bundle**，fixtures/line-crossing.neui + provenance，bundle_sha256 `a9117afc…`/ui_tbs `fc8bd177…` 与 App 仓 committed 证据字节一致） | 7 tests 0 fail（两轮全绿） | 真实签名 runtime 的严格 validateHello/validateGrant 消费 Host HELLO/GRANT（identity 10 字段/TBS/boot/session/ui/service 世代全真实）；snapshot.read REQUEST/RESULT 全链路 APPLIED 并渲染真实业务 UI（lc-app-state available）；真实 REQUEST/RESULT 负例消费（report-policy/enable/live UNAVAILABLE 理由透传至 App UI）；**video.open REQUEST → 限权媒体授权 RESULT → 真实 videoWorker/JMuxer/MSE 在沙箱内 320x180 live 解码播放**（显式用户 retry 动作触发）；config.apply REQUEST（payload.session_generation+expected_config_generation+candidate 经 broker 提升）→ CONFLICT `STALE_CONFIG_GENERATION`；counters.reset 无令牌 → REJECTED `ADMIN_CONFIRM_REQUIRED` → Host 管理员 confirm → 严格 EVENT（App 校验通过、会话不 revoke）→ App 重试 → APPLIED（服务端令牌按 grant 消费）；世代切换 → REJECTED IDENTITY_OR_GENERATION_CHANGED → REVOKE 至 App → 旧 frame 销毁 + 旧世代资产 404；沙箱内伪造窗口消息被 Host 忽略且会话不受扰；同 site 拒绝（不建 iframe）；签名资产精确路由（真 bundle 七文件 sha256/长度逐项校验，unknown/穿越/旧世代/tamper 一律 404 + no-store）；**B3 sandbox 默认矩阵：生产 sandbox=`allow-scripts allow-same-origin`（无 allow-downloads）时沙箱内 blob anchor 下载被 Chrome 拒绝（无 download 事件），仅 allow-downloads 对照帧同一动作成功下载（fixture-only 对照，非生产路径）** |

既有回归（0 fail）：app_report 552、policy(=app_host_control) 575、admission 179+97+84+61、control 441+107、run 214、session 540、installer 271+280+173+54、state 188+260、sidecar 155、sidecar_lifecycle 351、validate 21；Web vitest 基线 h264Player 既有 1 失败保持不变。ARM 编译：`make app` 与 `make app APP_HOST_POC=1` 均通过（PoC 关闭时本卡代码整段条件编译剔除）。docs-sync：`python3 Script/gen_web_api_docs.py --check` 通过。

## 5. 生产集成边界 / NOT_RUN

- 本卡 Chrome E2E 的消费者是**已验收 App #32 生产签名 bundle 的真实 runtime**（严格 validateHello/validateGrant/validateResult/validateEvent/validateRevoke），不再是宽松 fixture stub；`tests/sidecar_host_bridge` 内已无自造 App 消费者（旧 `app/app-main.mjs` Blob-ping fixture 已删除）。
- fixture 服务器（server.mjs）是 Host 测试自有 HTTP 桥（与生产 C 同一 API 契约的 host 可执行替身）；生产 C 路径由 tests/web_sidecar_bridge 534 checks 背书。video.live 场景代表“软件实验室中媒体授权已绑定”的 v1 op 合同演示，生产 C 的 video.open/close 在真实媒体授权落地前恒 UNAVAILABLE（fail-closed，不伪造）。
- 真机 LittleFS/RAM/PSRAM 预算、目标板 DNS/TLS/独立 App site 部署、真实摄像头/8081 流/WS/MSE 授权绑定、掉电、生产签名与安装许可：NOT_RUN（#40/#48/#58 现场验证项）。`app_bridge_app_site_origin_set` 的部署接线在设备侧保持 NULL=fail-closed（Manage/Status，无 iframe）。
- FileSaver `saveAs`（#99 FAIL）兼容状态保留：本卡未宣称完整录制导出 PASS；sandbox 默认无 allow-downloads；生产下载能力待独立 ACTIVE signed App-site + Host-scoped grant + 显式用户导出 gate 证明后方可启用受限路径。视频能力在获得真实媒体授权前恒为 Unavailable。
- #88 policy、#30 Enable、#91/App #27 live 未受审接入 → 全部 UNAVAILABLE，不从 report window/旧 v3 虚拟能力；Host-owned MQTT/Webhook/backlog Save 未显示任何成功。
