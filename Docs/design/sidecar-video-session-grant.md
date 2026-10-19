# Sidecar 视频播放会话接入（保留现有 8081 LAN 直连）设计

- Issue: #109
- 前置: #84（typed sidecar bridge）、App 仓 ne30x-app #32（已验收 VideoPreview/H264 Worker/MSE）
- 用户裁决（2026-10-19，CURRENT USER DECISION）: 保留 `ws://<NE301>:8081/stream` 无认证 LAN 直连、帧格式与旧客户端兼容；不对全部视频流统一加认证；可管理的是"已签名 App 的播放会话与其 Host bridge grant"，不承诺摄像头级全局访问撤销。该残余风险为用户明示接受。

## 1. 目标与非目标

目标：让当前已签名且 ACTIVE 的 Sidecar（App）通过既有 `ne301.sidecar.bridge.v1` 的 `video.open`/`video.close` 受限 op，合法取得既有视频源连接信息并管理自身播放会话生命周期。

非目标：统一认证 8081/stream、全局摄像头访问保密/撤销、影响旧 LAN 客户端、另造视频服务器、修改 App 已验收 UI/Worker、生产签发。

## 2. Host C 侧（`Custom/Services/Web/api/api_app_session_module.c`）

`video.open` / `video.close` 由"恒 UNAVAILABLE"升级为真实控制面。所有既有前置校验保持不变且先于 video 语义执行：grant 存在、未撤销/未过期、Sidecar descriptor 同步（committed native + trust anchor + ACTIVE 生命周期）、grant 身份与世代匹配（publisher key / native+UI TBS / service generation / native session generation）、op 在 grant capabilities 内、request_id 防重放、HTTP 侧管理员高权限认证。

### video.open 决策链（顺序即实现顺序）

1. `payload` 对象必须存在，否则 `REJECTED / PAYLOAD_REQUIRED`。
2. `payload.session_generation`（u32）必须存在且等于 grant 的 `native_session_generation`，否则 `REJECTED / SESSION_GENERATION_REQUIRED` / `REJECTED / STALE_SESSION_GENERATION`（不信 App 自报，只认 Host 自己登记的世代）。
3. `websocket_stream_server_is_running()` 为真，否则 `UNAVAILABLE / VIDEO_SERVICE_UNAVAILABLE`。
4. `websocket_stream_server_get_stats()` 成功且 `stream_active` 为真，否则 `UNAVAILABLE / VIDEO_SOURCE_UNAVAILABLE`（无视频源时明确拒绝，不发地址）。
5. 视频地址服务器侧构造，绝不取自 App：优先模块覆盖 `app_bridge_video_lan_host_set()`（合法字符校验），否则取本次请求 `Host` 头的主机部分（剥端口；`[IPv6]` 括号形式保留；非法字符拒绝），两者皆无 → `UNAVAILABLE / VIDEO_ENDPOINT_UNAVAILABLE`。URL 固定为 `ws://<host>:8081/stream`，与现有服务器默认配置一致（`web_service.c` 使用 `websocket_stream_get_default_config`，port 8081 / path `/stream`）。
6. 成功：grant 记 `video_session_open = TRUE`，返回 `APPLIED`，payload 恰好四键（App #32 `validateVideoPayload` 按 exact-keys 校验，禁止多余字段）：
   - `url`：上述有界 LAN 地址（≤128 字节）
   - `session_generation`：grant 的 native session generation
   - `expires_at_ms`：grant 的 `expires_ms`（会话有效期以 grant 为界，天然有界）
   - `grant_id`：本次 grant id（64 hex）
   重复 open 幂等返回同一有界信息。不返回、不外泄管理员 cookie/token/私钥。

### video.close 决策链

1. payload 必须存在；`session_generation` 同上校验。
2. `payload.grant_id` 必须等于本 grant id，否则 `REJECTED / GRANT_MISMATCH`。
3. 未 open 过 → `REJECTED / VIDEO_SESSION_NOT_OPEN`（不假 APPLIED）。
4. 成功：清 `video_session_open`，返回 `APPLIED` + `{session_generation}`。

撤销/过期/换包/失活的既有路径共用：调用即撤销 grant 并回 `REJECTED + revoked=true`，App 侧由此收 REVOKE 并清理自己的 WS/Worker/MSE。grant 结构新增的 `video_session_open` 随 grant 分配 memset 清零、随 `app_bridge_grants_reset()` 全量复位。

该 grant 只约束受信 Sidecar/UI 的功能面；8081 网络入口本身没有也不会加认证（用户裁决）。

## 3. Host Web 侧

- `Web/src/types/sidecar.ts`：`SIDECAR_UNAVAILABLE_OPS` 移除 `video.open`/`video.close`（不再属于"永久不可用"ops）。
- `sidecarAppFrame.tsx` 既有 onResult→`VideoSessionTracker` 映射不变：APPLIED→Live（会话面）、UNKNOWN→Reconnecting、其余→Unavailable；broker revoke→Unavailable。播放像素级 Live/Reconnecting 判定属 App #32 VideoPreview（已验收，只读）。
- Broker 的 RESULT 透传（`sidecarBridge.ts`）原样携带四键 payload，未改动。

## 4. App #32 消费契约（只读参照，ne30x-app）

`runtimeCapabilities.openVideoStream`：发 `video.open {session_generation}` → `APPLIED` 且 payload 通过 exact-keys 校验、`session_generation` 与请求一致、`expires_at_ms > now` 才置 available；`release()` 发 `video.close {session_generation, grant_id}`；VideoPreview 依 `expiresAtMs`/`wv_close`/`wv_error` 转 Reconnecting/Unavailable 并销毁 Worker/MSE。本卡 Host 侧行为与其完全对齐（C 套件按该契约断言四键与世代）。

## 5. 测试矩阵

### C 集成（`tests/web_sidecar_bridge`，真实模块编译，775/0）

- 无视频源：`VIDEO_SOURCE_UNAVAILABLE`；服务未运行：`VIDEO_SERVICE_UNAVAILABLE`；端点未知（无覆盖且无 Host 头）：`VIDEO_ENDPOINT_UNAVAILABLE`；非法覆盖串被拒。
- 正常取流：`APPLIED` 四键精确（url/session_generation/expires_at_ms/grant_id）、URL `ws://10.0.0.42:8081/stream`、世代/有效期与 grant 一致；幂等 open。
- Host 头派生：`Host: 192.168.7.55:80` → `ws://192.168.7.55:8081/stream`；`Host: [fd00::7]:8080` → `ws://[fd00::7]:8081/stream`。
- close：缺 payload→`PAYLOAD_REQUIRED`；缺/错 grant_id→`GRANT_MISMATCH`；未 open→`VIDEO_SESSION_NOT_OPEN`；成功→`APPLIED {session_generation}`，再 close→`VIDEO_SESSION_NOT_OPEN`；open→close→再 open→再 close 全链。
- 既有回归：grant 身份/世代/RNG/重放/撤销/confirm 全保留（test_unavailable_ops 中 video 两 op 由恒 UNAVAILABLE 改为按真实语义 REJECTED）。

### 浏览器 E2E（`tests/sidecar_host_bridge`，真实 Chrome + 已验签 App #32 bundle + 真实 broker，11/11）

- 正常取流（既有）：video.open 经真实 broker，Worker/JMuxer/MSE 解码 320x180 出画。
- 无视频源（新）：`videoSourceActive=false` → `UNAVAILABLE / VIDEO_SOURCE_UNAVAILABLE`，App 停在 unavailable，流服务器 0 连接，永不假 Live。
- 旧 LAN 直连不回归（新）：无任何 grant/token 的普通 WebSocket 客户端连 `/stream` 直收 WSFS 头帧——用户裁决的兼容面被显式钉住。
- WS 断线恢复及无重连授权（新）：销毁流连接并停听 → App 转 reconnecting → 同端口恢复 → 复为 live，全程 video.open 次数不增（重连复用已授 URL）。
- 撤权/iframe teardown（新）：管理员 revoke → broker REVOKE → iframe 销毁 → App 流 socket 关闭 → 无后续 video.open。
- 既有回归：世代切换撤销、伪造 postMessage 拒绝、同 site 拒绝、sandbox 下载门、confirm 单次 token 不泄漏。

### Web vitest（176 项，唯一失败=既有 h264Player）

- broker：APPLIED 四键原样透传；UNAVAILABLE 不动会话信任；revoked → 全会话撤销 + 端口关闭；close APPLIED 透传；`SIDECAR_UNAVAILABLE_OPS` 不含 video ops。
- frame：video.open/close 结果驱动受信视频状态条（Unavailable↔Live）。
- 附带修复两处测试工具既有 `tsc -b` 报错（`pending` 参数显式类型），`pnpm build` 恢复绿色。

## 6. 边界与残余风险

- 8081 直连入口未加认证、旧 LAN 客户端不受 App 会话撤销约束——用户明示接受的取舍；撤销只停止"该 App 自己"的播放会话。
- E2E 的 8081 服务器为测试内仿制（镜像真实服务器的无认证 `/stream` 升级行为与 WSFS 帧）；真实目标板摄像头链路未在软件测试内覆盖（NOT_RUN，现场验收属 AC4 设备项）。
- App site CSP 对 `ws://<device>:8081` 的 connect-src 允许性属 #48 真机验收项。
