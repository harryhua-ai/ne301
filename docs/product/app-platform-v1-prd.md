# NE301 独立 App 平台 PRD v1.0 — Draft

> 状态：**DRAFT / 待 User 联合评审与范围批准**；维护人：Role A / Project Steward；提出日期：2026-10-15。
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
| **PLAT-01** | 可信 App 安装、查询、启动/运行状态、升级、卸载。拒绝坏签名/不匹配发行者、App、key、ABI/版本/尺寸；失败不覆盖基础 Web、现有原生包和用户状态。 | 已受审安装/准入基础、[#40](https://github.com/harryhua-ai/ne301/issues/40)、[#48](https://github.com/harryhua-ai/ne301/issues/48) |
| **PLAT-02** | 独立业务 UI Sidecar 根据 [#79 §12](https://github.com/harryhua-ai/ne301/issues/79) 的 NEUI v1 与当前受信 native `publisher_id/app_id/publisher_key_sha256/native_tbs_sha256` 精确绑定；整包/文件验真、版本切换、满盘/半写/恢复、卸载/换包/失权撤销；不修改冻结 `.neapp` v1/v2/v3。 | [#86](https://github.com/harryhua-ai/ne301/issues/86)、[#83](https://github.com/harryhua-ai/ne301/issues/83) |
| **PLAT-03** | 业务 UI 与 Host/其他 App 的管理员凭据、管理 DOM、存储和任意 API 隔离；仅向**当前**已授权 App/Sidecar 世代开放可撤销 typed capability；禁止将管理员 token/cookie 交给 iframe。 | [#84](https://github.com/harryhua-ai/ne301/issues/84) |
| **PLAT-04** | 受信运行会话中的现有业务配置经 v3 `CONFIG_APPLY`、generation CAS 与实际 `APPLIED` 回执确认；重置/危险操作明确确认，未知或冲突不得提示保存成功。**另需支持经受信授权、可回读的业务 Enable 暂停/恢复操作**：暂停计数不停止/卸载整个 App、不清除累计结果，恢复开始新窗口；不得混淆 Host App start/stop 或擅改冻结 v3 144B config-only。平台不解释 App 私有 blob；Enable 的新最小能力尚待单独冻结与验证。 | [#56](https://github.com/harryhua-ai/ne301/issues/56)、[#84](https://github.com/harryhua-ai/ne301/issues/84) |
| **PLAT-05** | 从当前受信 App 读取有界累计/窗口计数、模型/会话/快照及其 fresh/stale/unavailable；为 App-owned 活动轨迹与有序 crossing events 提供**版本化、受信、有界、可声明缺口**的读取能力。不得把窗口报告接成每 2 秒实时 feed 或私改冻结 v3 ABI。 | [#87](https://github.com/harryhua-ai/ne301/issues/87)、[#91](https://github.com/harryhua-ai/ne301/issues/91)、[#84](https://github.com/harryhua-ai/ne301/issues/84) |
| **PLAT-06** | 为正式业务页提供实际 H264/视频会话受控能力与撤销；视频独立反馈 Live/Reconnecting/Unavailable，不以 App Running 或旧冻结帧冒称实时。 | [#84](https://github.com/harryhua-ai/ne301/issues/84)、配对 App PRD |
| **PLAT-07** | Host 拥有当前 App 专属 MQTT/Webhook 分发开关及可实际生效、可读回、资源有界、身份隔离的 Backlog 策略；App/Host 两组**独立提交与独立结果**，不覆盖设备级通道凭据/URL/topic。**User 已决定首版采用易失报告缓存：断网时在有效容量内暂存，网络恢复后尽力补发；设备断电/重启后未发送报告允许丢失，不承诺重启重放或 exactly-once。** 保存/恢复的是**传输策略设置**，不是保证报告队列持久化；队满、溢出、重启、送达未知必须如实可观察。 | [#75](https://github.com/harryhua-ai/ne301/issues/75)、[#89](https://github.com/harryhua-ai/ne301/issues/89)、[#88](https://github.com/harryhua-ai/ne301/issues/88) |
| **PLAT-08** | 相同设备上受信原生/Sidecar 的更新、掉电中断/未知、空间不足、会话失效、未授权调用都可诊断并 fail closed；不得伪报“两个工件原子安装”或未测得的空间足够。 | [#83](https://github.com/harryhua-ai/ne301/issues/83)、[#48](https://github.com/harryhua-ai/ne301/issues/48)、[#58](https://github.com/harryhua-ai/ne301/issues/58) |

## 4. 统一跨仓验收门

每个 Must Have 须追溯到真实 App producer、Host capability、浏览器 UI、跨仓正/负例与必要真机结果；**某单仓库或离线测试 DONE 只关闭对应工程 Issue，不能单独宣称产品功能 DONE**。

- 独立更新：业务 App 或 UI 新版本安全换入，Host 固件/基础 Web SHA 不因该次业务更新改变；失败不损坏原可信版本（具体实际设备验证属于 M3）。
- 同一受信身份：换包、卸载、撤销、高权限失效后旧 UI、WS、媒体和读写 capability 不再访问新 App；错误版本/坏签名拒绝。
- 配置：App 业务保存与 Host 传输策略分别确认，单边成功/失败/UNKNOWN 各自反馈；无全局设备设置的隐式修改。
- 业务：旧 `counting` 的 IN/OUT、活动轨迹/事件、视频、画线与报送可从真实生产路径观察；`window report tracks[]` 不能当实时轨迹。
- 验收报告标明 `PASS / FAIL / NOT_RUN / UNKNOWN`、exact source SHA、设备/网络边界和故障恢复条件。

## 5. 首版明确不做

不交付第三方 App 商店/在线商店、无信任第三方原生代码沙箱、无界缓存或任意动态扩容、MQTT/Webhook 远端 exactly-once 送达承诺、通用工作流/多 App 任意并发产品、为业务 UI 把组件编回 Host。当前 User 已决定**不以强制 HTTPS 或抵御主动 LAN MITM 作为本阶段验收条件**；此例外不豁免签名、权限、来源隔离、撤销与失败保护，也不证明设备永不联网。

## 6. 已确认与待决定的产品差额

**已确认：** 正式业务 UI 使用 `ne301/counting@de25a6f1f431a631a37ff1cda87f616e293e8836`，不是 `people-counting` 备份；两点定位后支持鼠标/触摸拖动任一端点；视频独立三态；Advanced 同页保留业务组与当前 App 专属 MQTT/Webhook/Backlog，**分别 Save**。详见 [#79 §10](https://github.com/harryhua-ai/ne301/issues/79)。

**新确认 / 待冻结分开：** (a) **User 2026-10-15 已确认**业务 `Enable` 独立于 Host App start/stop：关闭暂停越线计数且累计保留，恢复从新统计窗口开始；可用且受信的最小启停接口和失败/状态回读仍未冻结，原 v3 144B config-only 没有此字段。(b) **User 2026-10-15 已选择 Backlog 方案 A（断网有限缓存/恢复后尽力补发；断电或重启允许丢失未送达报告）**，不要求可靠持久报告队列；但旧控件 1..256 的**实际可支持范围仍待板端资源验证及独立产品复核**。[#89](https://github.com/harryhua-ai/ne301/issues/89) 已完成离线调查，不是 256 条实装可用证明。(c) 实时 App→Host 新 ABI/性能额定（[#91](https://github.com/harryhua-ai/ne301/issues/91)）；(d) Sidecar 真实存储资源/切换/恢复；(e) 新安装/换包/失权的用户可见关键状态/导航细节仍需 User UI 审阅。**Draft 不把这些写成已实现或已批准的缩减。**

## 7. 范围变更与文档治理

此 PRD 经 User 明确联合批准前始终为 Draft；不替代 [#79](https://github.com/harryhua-ai/ne301/issues/79) 已有技术决策或各 Issue 唯一 `ght-contract`。批准后新增/改变产品能力需标记 `PLAT-ID` 或配对 `APP-ID`、列出是否在 M1/M2/M3、验收变化、依赖与风险，由 A 审查、涉及实质产品取舍则由 User 批准。普通 B HOW、测试补全、源码调试不自动升级成新产品需求或新 Issue。不得为本 PRD 建第二份进度/授权表；真实状态从 GitHub Claim/PR/Issue/Plan 读取。

**参考：** [平台路线图](https://github.com/harryhua-ai/ne30x-app/blob/main/docs/app-platform-development-roadmap.md)（长期方向，可能含旧快照）、[#79](https://github.com/harryhua-ai/ne301/issues/79)（当前架构决策）、[#58](https://github.com/harryhua-ai/ne301/issues/58)（工程结项/发布边界）。