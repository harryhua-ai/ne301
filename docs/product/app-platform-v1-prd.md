# NE301 独立 App 平台 PRD v1.0 — Frozen Product Scope

> 状态：**FROZEN PRODUCT SCOPE v1.0 / 2026-10-15**（User 已要求完成联合 PRD 冻结；17 项 Must Have 的产品 WHAT/DONE 与 M1/M2/M3 同时成套冻结）；维护人：Role A / Project Steward。此状态**不是**功能已实现、User 的最终产品验收、真机操作、READY 或生产发布授权。
> 产品：NE301 独立 App 平台 + 首个 Line Crossing。配对 PRD：`harryhua-ai/ne30x-app/docs/product/line-crossing-v1-prd.md`（同批审阅）。
> 本文是**平台用户能力、范围和产品 DONE**的候选定义，不是 `ght` Contract、Execution Plan、代码实现授权、真机许可或生产发布批准。既有 Contract/Claim/PR 的当前状态始终以 GitHub durable truth 为准。

## 1. 目标和首版用户

**主要用户：** 在受管理的设备/LAN 上运营 NE301 的授权管理员；首个业务 App 为 Line Crossing。

**首版产品目标：** 用户通过既有 NE301 Web 管理一个受信独立 App，完成验签、安装、运行、更新、卸载，并使用它独立交付的正式 Line Crossing 业务页面查看真实计数/轨迹/事件、播放视频、编辑/保存配置及按策略上报；**后续替换原生业务 App 或其 UI 不需重新编译、刷写 Host 主固件或覆盖基础 Web**。在实际获授权设备上验证失败保护、身份、恢复和业务端到端闭环。实验版工程验收不等于生产发布。

## 2. 范围分层（单一产品的三个里程碑）

| 里程碑 | 用户可观察结果 | 验收级别 |
| --- | --- | --- |
| **M1 核心纵向集成** | 可信 native + 独立签名 Sidecar 可安全安装/加载/替换；独立业务 UI 可打开、读取当前受信 App 状态并经受控桥接提交业务配置，失败/失权准确呈现 | **内部工程门**；可在隔离软件环境验证，不代表真机/功能完整 |
| **M2 完整 Line Crossing 功能** | 联合 App PRD 中的 Realtime、Config、Advanced、视频、真实轨迹/事件和报告策略达到同等用户目标；安全/失败状态可用 | **首版完整功能门**，跨仓联合验证；不得以窗口历史代替实时 |
| **M3 设备与工程结项** | 在有独立许可、备份与恢复计划的同一指定设备上完成真实 AI/视频/报告、App 与 UI 分别替换、重启/失败与持续性验证 | 工程验证；实际 NOT_RUN 不得标 PASS，User 产品接受/正式发行另行决策 |

**M1 不是缩减后的首版**；所有 M2/M3 Must Have 不因先行内部演示而消失。未批准真实设备写入、签发或外发时，只能进行不越权的离线/只读工作。

## 3. Must Have：NE301 平台责任

| ID | 平台产品能力与 DONE | 主要对应现有工作 |
| --- | --- | --- |
| **PLAT-01** | 可信 App 安装、查询、启动、运行状态、升级、卸载；拒绝坏签名、发行者/App/公钥/ABI/版本/尺寸不匹配，不影响基础 Web、其他 App 或全局设备配置。**卸载是会清除该 App 专属数据的破坏性操作，必须在卸载之前让有权限管理员明确确认**，清楚列出将删除的业务配置、累计 IN/OUT、App 持久状态、逐 App MQTT/Webhook/Backlog 策略及未发报告；取消确认不卸载、不删除。确认后才卸载、撤销旧权限并清除该 App 全部专属数据；**同一受信 App 重新安装从初始配置和零累计开始，不恢复旧数据，也不弹恢复确认**。正常升级不是卸载，不清空兼容的旧业务配置/累计。 | 已受审安装/准入基础、[#40](https://github.com/harryhua-ai/ne301/issues/40)、[#48](https://github.com/harryhua-ai/ne301/issues/48) |
| **PLAT-02** | 独立业务 UI Sidecar 根据 [#79 §12](https://github.com/harryhua-ai/ne301/issues/79) 的 NEUI v1 与当前受信 native `publisher_id/app_id/publisher_key_sha256/native_tbs_sha256` 精确绑定；整包/文件验真、版本切换、满盘/半写/恢复、卸载/换包/失权撤销；不修改冻结 `.neapp` v1/v2/v3。 | [#86](https://github.com/harryhua-ai/ne301/issues/86)、[#83](https://github.com/harryhua-ai/ne301/issues/83) |
| **PLAT-03** | 业务 UI 与 Host/其他 App 的管理员凭据、管理 DOM、存储和任意 API 隔离；仅向**当前**已授权 App/Sidecar 世代开放可撤销 typed capability；禁止将管理员 token/cookie 交给 iframe。 | [#84](https://github.com/harryhua-ai/ne301/issues/84) |
| **PLAT-04** | 受信运行会话中的现有业务配置经 v3 `CONFIG_APPLY`、generation CAS 与实际 `APPLIED` 回执确认；重置/危险操作明确确认，未知或冲突不得提示保存成功。**另需支持经受信授权、可回读的业务 Enable 暂停/恢复操作**：暂停计数不停止/卸载整个 App、不清除累计结果，恢复开始新窗口；不得混淆 Host App start/stop 或擅改冻结 v3 144B config-only。平台不解释 App 私有 blob；Enable 的新最小能力尚待单独冻结与验证。 | [#56](https://github.com/harryhua-ai/ne301/issues/56)、[#84](https://github.com/harryhua-ai/ne301/issues/84) |
| **PLAT-05** | 从当前受信 App 读取有界累计/窗口计数、模型/会话/快照及其 fresh/stale/unavailable；为 App-owned 活动轨迹与有序 crossing events 提供**版本化、受信、有界、可声明缺口**的读取能力。不得把窗口报告接成每 2 秒实时 feed 或私改冻结 v3 ABI。 | [#87](https://github.com/harryhua-ai/ne301/issues/87)、[#91](https://github.com/harryhua-ai/ne301/issues/91)、[#84](https://github.com/harryhua-ai/ne301/issues/84) |
| **PLAT-06** | 为正式业务页提供实际 H264/视频会话受控能力与撤销；视频独立反馈 Live/Reconnecting/Unavailable，不以 App Running 或旧冻结帧冒称实时。 | [#84](https://github.com/harryhua-ai/ne301/issues/84)、配对 App PRD |
| **PLAT-07** | Host 拥有当前 App 专属 MQTT/Webhook 分发开关及可实际生效、可读回、资源有界、身份隔离的 Backlog 策略；App/Host 两组**独立提交与独立结果**，不覆盖设备级通道凭据/URL/topic。**User 已决定首版采用易失报告缓存：断网时在有效容量内暂存，网络恢复后尽力补发；设备断电/重启后未发送报告允许丢失，不承诺重启重放或 exactly-once。User 已选择 Backlog 容量方案 B：保留可调容量，但上限由目标设备实测可持续负担的资源、按 App 公平配额与队列行为决定；Host 提供实际可用范围和生效值的可靠读回，前端按该范围限制并明确提示越界/资源不足，不能固定宣称 256 或默默缩为 8。** 关闭某个已启用的 MQTT/Webhook 分发开关并成功保存后，**取消该通道尚未开始发送的积压任务，另一通道的待发送、发送中与结果保持不变**；随后产生的报告也不再进入关闭的通道。已经发起到外部系统的请求无法保证撤销：必须按实际发送结果或 UNKNOWN 反馈，不能虚报已取消。重新启用不得自动重播已取消的旧积压报告。** 保存/恢复的是**传输策略设置**，不是保证报告队列持久化；队满、溢出、重启、送达未知必须如实可观察。 | [#75](https://github.com/harryhua-ai/ne301/issues/75)、[#89](https://github.com/harryhua-ai/ne301/issues/89)、[#88](https://github.com/harryhua-ai/ne301/issues/88) |
| **PLAT-08** | 受信 App/Sidecar 安装、更新、卸载在掉电、满盘、半写、身份/会话失效、权限失败和结果 UNKNOWN 时必须 fail closed，不能虚报 native/UI 双工件原子安装。**卸载前**取得授权管理员对删除范围的一次明确确认；确认后撤销该 App 的原生/Sidecar/iframe/媒体/报告 capability，并安全清除该 App 的业务配置、累计、持久状态、逐 App 传输策略及待发报告；不清理其他 App、全局 MQTT/Webhook 凭据/地址/topic 或必要审计记录。任何部分清除/中断/未知均不可提示“卸载且清除完成”；同一受信身份重装不恢复历史数据。安装失败、常规升级和业务 Enable 暂停都不能误触发数据清除。 | [#83](https://github.com/harryhua-ai/ne301/issues/83)、[#48](https://github.com/harryhua-ai/ne301/issues/48)、[#58](https://github.com/harryhua-ai/ne301/issues/58) |

## 4. 统一跨仓验收门

每个 Must Have 须追溯到真实 App producer、Host capability、浏览器 UI、跨仓正/负例与必要真机结果；**某单仓库或离线测试 DONE 只关闭对应工程 Issue，不能单独宣称产品功能 DONE**。

- 独立更新：业务 App 或 UI 新版本安全换入，Host 固件/基础 Web SHA 不因该次业务更新改变；失败不损坏原可信版本（具体实际设备验证属于 M3）。
- 同一受信身份：换包、卸载、撤销或管理员失权后旧 UI、WS、媒体和读写 capability 均失效；错误签名/版本拒绝。**确认卸载后该 App 全部专属数据同时清理，不因相同发布者/App ID 或公钥为重装保留可恢复副本**；再次安装从新的业务配置和零累计开始。普通升级保留受审的业务配置/累计，不等于卸载。
- 配置：App 业务保存与 Host 传输策略分别确认，单边成功/失败/UNKNOWN 各自反馈；无全局设备设置的隐式修改。
- 业务：旧 `counting` 的 IN/OUT、活动轨迹/事件、视频、画线与报送可从真实生产路径观察；`window report tracks[]` 不能当实时轨迹。
- 验收报告标明 `PASS / FAIL / NOT_RUN / UNKNOWN`、exact source SHA、设备/网络边界及故障恢复条件。卸载验收至少覆盖：用户取消确认则不卸载也不清数据；确认后撤销旧原生/Sidecar/iframe/媒体权限，清理该 App 配置、累计、持久状态、逐 App 策略与待发报告，其他 App/全局设置不变；失败/半写/掉电不谎报完成；重新安装**即使受信身份相同**也为初始配置和零累计，不提供旧数据恢复路径；普通升级不误删旧业务数据。

## 5. 首版明确不做

不交付第三方 App 商店/在线商店、无信任第三方原生代码沙箱、无界缓存或任意动态扩容、MQTT/Webhook 远端 exactly-once 送达承诺、通用工作流/多 App 任意并发产品、为业务 UI 把组件编回 Host。当前 User 已决定**不以强制 HTTPS 或抵御主动 LAN MITM 作为本阶段验收条件**；此例外不豁免签名、权限、来源隔离、撤销与失败保护，也不证明设备永不联网。

## 6. 已冻结的产品决定与待工程验证条件

**首版已冻结的可见体验：** 正式 `counting@de25a6f1f431a631a37ff1cda87f616e293e8836` Realtime/Config/Advanced；双端点鼠标/触摸拖动；视频 `Live/Reconnecting/Unavailable` 独立于 App Running；Config `Enable` 只暂停/恢复计数、保留累计、恢复时开始新窗口；Advanced 的 App/Host 两组独立 Save 与独立结果。

**Backlog 与传输：** 报告缓存为有限易失队列，断网后按实际容量暂存、恢复后尽力补发，断电/重启允许丢未发送项；容量仍可配置，实际上限以目标设备资源与公平性实测为准，**不承诺 256 或直接固定 8**；关闭 MQTT/Webhook 某通道成功后，仅取消该通道尚未开始发送的报告，其他通道不变；已经发起的发送无法保证撤销。

**卸载即删除、重装从零（User 最后明确纠正，覆盖任何“卸载后保留/重装恢复”旧建议）：** 卸载前先清晰展示专属配置、累计 IN/OUT、持久业务状态、逐 App 上报策略和未发报告将被删除，由授权管理员确认；取消即保持现状。确认后撤销当前受信 App/Sidecar/媒体/browser 权限、卸载并清除全部该 App 专属数据，失败/UNKNOWN 不假报完成；不触及其他 App、全局网络设置及必要审计。即使日后重新安装完全相同的受信 App，也从全新配置和零累计开始，**不自动恢复旧业务数据**；普通版本升级不是卸载，不清零。

**仍待工程取证和 READY 前设计确认，均不是开放的首版产品范围选择：** 业务 Enable 所需的新受信控制能力（冻结 v3 144B 不可偷改）；实时 App→Host 下一版本 ABI、容量/延迟及失效处理；目标设备 Backlog 最大实测容量；Sidecar 安装的 RAM/FS 和掉电/半写恢复；安装/卸载/失权关键界面的审阅示意及具体导航/错误反馈。证据不足时必须拒绝伪成功，保留既定产品验收门，不可由 B 缩减功能。未来关键 UI 仍须按 Role A 规则经 User 交互设计确认才可 READY。

## 7. 范围变更与文档治理

此 PRD 的 **PLAT-01..08 与配对 APP-01..09 产品范围已联合冻结 v1.0**；不替代 [#79](https://github.com/harryhua-ai/ne301/issues/79) 已有技术决策或各 Issue 唯一 `ght-contract`。批准后新增/改变产品能力需标记 `PLAT-ID` 或配对 `APP-ID`、列出是否在 M1/M2/M3、验收变化、依赖与风险，由 A 审查、涉及实质产品取舍则由 User 批准。普通 B HOW、测试补全、源码调试不自动升级成新产品需求或新 Issue。不得为本 PRD 建第二份进度/授权表；真实状态从 GitHub Claim/PR/Issue/Plan 读取。

**参考：** [平台路线图](https://github.com/harryhua-ai/ne30x-app/blob/main/docs/app-platform-development-roadmap.md)（长期方向，可能含旧快照）、[#79](https://github.com/harryhua-ai/ne301/issues/79)（当前架构决策）、[#58](https://github.com/harryhua-ai/ne301/issues/58)（工程结项/发布边界）。