# App 业务结果回传与 NE301 MQTT / HTTP(S) Webhook 上行（Issue #44 设计）

状态：Implemented（B 实现，待 A 审查）
关联：ne301#44；依赖 ne301#31/#37/#47（已 canonical DONE）；并行边界：#43（AI 事件 + 会话 API）、#46（app_state）

## 1. 目标与边界

App（如 Line Crossing）通过 Host 提供的受信提交接口上传业务统计报告，Host 在**接受/入队前**完成校验、身份规范化与配额检查，再经**既有** MQTT 与 HTTP(S) Webhook 两条通道上行。本设计不引入第二套网络栈、不改 Web 配置页语义、不改 v1 ABI、不重实现 #47 验签、不承诺 exactly-once。

并行隔离：本分支不修改 `app_host_run.c`（仍为 v1 运行路径）；v2 API 表（48B，`report_submit` 在表偏移 28、`report_status` 在偏移 32）与会话生命周期接线由 #43 的会话机制在合并时接入。本 Issue 交付**提交服务的全部语义**与**固件双通道适配器**，测试以明示的隔离 session fixture + fake transport 完成。

## 2. 模块

| 文件 | 内容 |
| --- | --- |
| `Custom/Services/AppHost/app_report_service.{h,c}` | 核心服务：会话门控、严格 JSON 校验、身份规范化、序号校验与去重、有界队列、双通道 pump、状态查询。无固件依赖，宿主可测。 |
| `Custom/Services/AppHost/app_report_service_device.{h,c}` | 固件适配：设备身份（factory 6B MAC 优先、Wi-Fi AP MAC 回退）、boot id（UID⊕tick）、MQTT/Webhook 传输适配、500ms pump 任务、`appreport` CLI（info/start/stop）。 |
| `Custom/Services/Webhook/webhook_service.{h,c}` | 新增 `webhook_service_push_json`：复用既有 http_client、webhook URL/TLS/认证配置的同步 `application/json` POST；2xx 即该次传输边界成功。不建第二套 HTTP 客户端，不影响原 JPEG multipart 路径。 |
| `Appli/Makefile`、`Custom/Core/Log/cli_cmd.c` | APP_HOST_POC 块编译两份新源文件；注册 `appreport` CLI。 |

## 3. 提交语义（对齐 v2 §6.6 与 A 2026-10-10/12/13 决定）

`app_report_submit(session, json, len, app_report_seq, &out_host_boot_id)` 返回码即 v2 §6.4 枚举。按序执行：

1. **会话门控（AC1）**：session 必须非空、`active`、与当前绑定会话（`publisher_id`/`app_id`/`publisher_key_sha256`/`report_max`）逐项一致，`report_max` 非零。任何不符 → `-3 UNAUTHORIZED`，不入队。会话由 Host 侧构造（#43 接线后来自 #47 验签判决），App JSON 不参与身份建立。
2. **参数**：`json` 非空、`len>0`、`[0,len)` 内无 NUL、`out_host_boot_id` 非空，否则 `-1`。
3. **配额（入队前）**：`len > session->report_max` 或 `len > 6144` → `-5 QUOTA_EXCEEDED`。不截断。
4. **去重**：以（当前 Host boot、publisher key SHA-256、app_id、`report_seq`）查 seen 环（16 槽）。命中 → 返回 `OK` 与原 boot id，**不再入队**（幂等，不重复上行）。
5. **严格 JSON 校验**（自研递归下降解析，无第三方依赖）：
   - 整个输入必须是单个合法 JSON object，顶层重复键一律 `-1`（含身份键重复：无法无歧义定位单一 `device_id/boot_id/report_seq`）；
   - `schema_version` 必须为整数 1（缺失/类型错 `-1`，值非 1 `-4 INCOMPATIBLE`）；
   - `type` 必须为字符串 `"line_counting"`（非字符串 `-1`，值不同 `-4`）；
   - JSON `report_seq` 必须为非负整数字面量且 **等于参数 `app_report_seq`**（否则 `-1`）；
   - `boot_id` 若存在必须是 u32 整数（任何值含 0 均接受——首报未知不是拒绝理由）；`device_id` 若存在必须是字符串；
   - `window/total/counter/target/model` 必须存在且为 object（既有消费方字段兼容，业务值不改动）。
6. **身份规范化（入队前，AC 核心）**：
   - `device_id` ← Host 可信设备身份：`factory_mac_get_burned()`（6B MAC，格式校验）→ 回退 `nm_get_netif_info(NETIF_NAME_WIFI_AP)`；两源皆不可用 → **fail-closed `-6 STORAGE_UNKNOWN`**，绝不透传 App 占位符 `ne301-app:line-crossing`、绝不采信 App JSON `app_id`；
   - `boot_id` ← 当前 Host boot id（UID⊕tick 派生，保证非 0），**替换** App 自报值（含首报 0）；
   - App 占位符/伪造 `device_id` 一律被可信值覆盖；`report_seq` 与业务字段逐字节保留；
   - 输出按顶层成员序重建（成员内 raw 字节保真，跨成员空白归一）；规范化净增 ≤ 59B，槽容量 6240B ≥ 6144+64，合法输入不因 Host 封装降级；
   - 设备身份格式有效性 ≠ 不可伪造/产线唯一——不承诺 MAC 抗篡改（A 证据边界）。
7. **入队**：写入有界队列（8 槽 × 6240B，静态）。槽满且无可逐出的终态槽 → `-7 BUSY`。仅当**确实入队**后才返回 `OK` 并输出 Host boot id。`OK` = 平台接受，非远端送达。

## 4. 队列 vs 送达（AC3）

- 每条报告双通道状态独立，冻结五枚举：`0=NOT_CONFIGURED / 1=PENDING / 2=TRANSPORT_SUCCEEDED / 3=TRANSPORT_FAILED / 4=UNKNOWN`。
- pump 每槽逐通道：channel ready（已配置启用）为假 → `NOT_CONFIGURED`（终态，不重试不计数）；尝试上限 3 次：传输返回 失败→重试直至上限→`FAILED`；成功→`SUCCEEDED`；返回 unknown（已发出但无法证实）→ `UNKNOWN`（终态，不重试，避免重复投递）。
- 双通道共享同一份已规范化终版字节（入队后不可变），pump 原样投递两路——同一条已校验报告身份。
- 入队成功 ≠ 任何远端确认；单边成功不改写另一边；查询不重新入队。历史零丢失/exactly-once 不承诺；终态槽可被新报告 FIFO 逐出（状态窗口有界）。
- 重启：队列易失，旧 boot id 查询如实 `NOT_FOUND`（2），不虚报不补发。

## 5. 固件传输边界

- MQTT：`mqtt_service_publish`（既有 data report topic + 配置 QoS）。QoS0：发布调用成功即该通道 `SUCCEEDED`（QoS0 的实际协议边界）；QoS1/2：发布成功后以 `mqtt_service_get_acked_msg_id` 有界等待 PUBACK（2s），命中本 msg_id → `SUCCEEDED`，超时 → `UNKNOWN`。
- Webhook：`webhook_service_push_json`（既有 URL/TLS/auth），2xx → `SUCCEEDED`，其余 → `FAILED`（可重试）。
- 会话尚无实装（#43 并行）：服务由操作者经 `appreport start` 显式启动；`appreport info` 输出 boot id、队列与每槽双通道状态（AC3 可观察性）。#43 接线后由会话建立方启动并绑定 verdict 派生的 session。

## 6. 测试（tests/app_report，宿主原生，238 checks 全绿）

覆盖：会话门控（未绑定/未激活/换 app/换钥后旧会话）、参数、配额（declared 与 6144 硬界）、严格 JSON 拒绝（15 组畸形输入）、schema 拒绝（版本/type/缺失字段/类型错/序号不符）、顶层重复键、身份规范化（占位符替换、伪造覆盖、缺省插入、boot 归一、首报 boot=0 接受）、序号空洞容忍（1→3 均接受）、同 seq 去重（幂等、不多发）、双通道字节一致与入队后不可变、通道四态组合（MQTT-only / Webhook-only / 双禁 / 单边失败独立可观察）、QoS 未知态不重试、重试上限→FAILED、队列满 BUSY、终态逐出、重启换 boot 旧查询 NOT_FOUND、6144B 边界接受与 6145B 拒绝、stats。

fake transport PASS ≠ 真实网络交付；真实 endpoint/外网发送/设备写入本轮 NOT_RUN。

## 7. 已知边界

- v2 48B API 表接线、真实会话建立与 `session_lifecycle` 归 #43；本分支 `app_host_run.c` 未改（零冲突面）。
- MQTT ack 等待为 pump 线程内有界阻塞；ack 队列溢出等记录丢失映射为 `UNKNOWN`，不冒充成功。
- seen 环逐出后同 seq 重提按新报告接受（有界窗口语义）；`NOT_CONFIGURED` 快照于首次 pump 时刻，后续启用不追溯。
- 服务生命周期为操作者显式 CLI 启动（无自动副作用），待 #43 会话机制接管。
