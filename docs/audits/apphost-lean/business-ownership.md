# 业务归属判定（跨仓迁移候选）— Lean AppHost R0（应 A 2026-11-05 合同增补）

固件 @ `cfb90ed22ecf3cf750ce08799140c2789e38df27`；App @ `460c8cd17c45d04aedf1cde0af4a8123502d91d7`。
判定纪律：**不因字段/文件带 Line Crossing 名称就断定可移动**；逐项核实执行环境、数据所有权、NPU/Camera 对接、持久化、ABI、报告传输契约。本文件只形成证据与拆分建议，不实际迁移、不改生产代码。

## 0. 扫描方法（可重放）

```sh
# 固件仓：业务字面全集（生产代码仅 4 处命中，见 §1）
git grep -inE "line_counting|line-crossing|linecrossing|LINE_CROSSING" -- Custom/
# 业务配置/字段名深扫（发现 §1-B1/B2 编解码块）
git grep -nE "counter_name|target_class_name|conf_threshold_permille|track_history_k|max_miss|k_confirm|window_minutes|heat_grid_enable|permille|0x3343434C" -- Custom/Services/Web/api/
# App 仓：同构编解码（证实双实现）
git grep -nE "0x3343434C|LC_CFG_CAND_SIZE|target_class_name" -- apps/line-crossing/app/
# 业务 profile 选择点全集
git grep -rn "V3_BUSINESS_LINE_CROSSING_64|V3_BUSINESS_GENERIC_FULL" -- Custom/ tests/
```

排除项同 dependency-closure.md §0（第三方库/历史 Docs/无关 tests）。

## 1. 候选清单与三归类判定

### B1｜LC 业务配置编解码（144B "LCC3" blob ↔ UI JSON）——【归类：迁往 App（长期）/ 暂留 Host（短期）】

- **现状**：`Custom/Services/Web/api/api_app_session_module.c:2228-2478`：`APP_BRIDGE_LC_CFG_SIZE=144`、magic `0x3343434C`（"LCC3"）、布局偏移 0/4/8/12/16/17/49/50/114-134；字段 `target_class_name`(≤32)、`counter_name`(≤64)、线几何 `x1/y1/x2/y2/outside_x/outside_y`(u16, permille)、`conf_threshold_permille`、`max_dist_permille`、`track_history_k`(4-16)、`max_miss`(1-255)、`k_confirm`(1-255)、`window_minutes`(1-1440)、`tracks_report_enable`、`heat_grid_enable`。消费点：bridge op `config.apply`/`config.read`/`counters.reset`（`:2476,2590,2682-2738,3226-3244`）。
- **双实现证据**：ne30x-app `apps/line-crossing/app/lc_cfgcodec.{c,h}` 同 magic `LC_CFG_MAGIC 0x3343434C`、同 `LC_CFG_CAND_SIZE 144`、同字段集——**同一 schema 两仓各写一份**。
- **执行环境/数据所有权**：字段语义（线几何、计数器名、跟踪参数）全部是 App 业务数据；Host 仅因 bridge op 需要"UI JSON → App 二进制"翻译而持有编码器。NPU/Camera 无关。持久化经 control 通道由 App 自管（lc_stateblob），Host 不落盘该 blob。
- **建议目标**：ne30x-app `apps/line-crossing/app/`（编码已存在）；Host 退化为 **opaque config 透传**（bridge op 载荷原样进 control 通道，长度 ≤ control_capacity 校验即可）。
- **Host 保留最小接口**：`config.apply/read` op 路由 + 授权 + 长度/代数（config_generation）检查——不含任何字段语义。
- **迁移障碍**：(a) bridge 线协议 `ne301.sidecar.bridge.v1` 的 op 载荷形状与 UI 契约（ne30x-app UI `ConfigPage/fields.tsx` 按当前 JSON 形状读写）；(b) App control 通道现在收 144B 二进制，改收 JSON/opaque 需 ne30x-app 侧 `lc_bus_config` 同步演进（该仓允许修改，但属跨仓同窗口变更）；(c) `web_sidecar_bridge`/`sidecar_host_bridge`/`counting_sidecar` 测试 fixture 绑定现形状。
- **最小隔离适配（不动 ABI 的当期可做项）**：把 `:2228-2478` 整块抽为独立翻译单元（建议名 `Custom/Services/Web/api/app_bridge_business_config.c`，无 v4/v5 代号），零行为变化，仅把"业务适配器"物理隔离出来，供未来整体 lift-out 审查。
- **验证要求**：M2.4 全套 + `tests/web_sidecar_bridge`（config op 向量）+ ne30x-app `tests/line-crossing-runtime/run.sh`（离线 bridge 契约）双仓联测；真机 `config.apply→snapshot 读回` 冒烟（待授权）。

### B2｜业务字段解释：`target_class_name` 投影进 live 载荷——【归类：迁往 App（随 B1）/ 暂留 Host（短期）】

- **现状**：`api_app_session_module.c:1099-1123` `app_bridge_snapshot_target_class()` 解析 App 业务配置 JSON 抽取 `target_class_name`；`:1519,1611-1654` 注入 live bridge 载荷（缺省 "unknown"）。
- **判定**：业务字段解释（哪个字段是"目标类别"、如何展示）属 App；Host live 传输层（环/GAP/STALE，`app_host_live.c`）本身业务无关（track 结构无业务字段，`app_host_live.h:120-190` 已核）。
- **障碍/最小适配**：live 载荷形状在 bridge 协议 v1 内（冻结）；随 B1 一起，由 App 在其快照/配置内自带展示元数据，Host 去掉对 `target_class_name` 的解析。当期最小隔离：并入 B1 的业务适配器翻译单元。

### B3｜报表内容 schema 校验（window/total/counter/target/model）——【归类：改为 Host 通用服务（需 A 裁决放宽）】

- **现状**：`app_report_service.c:1055-1104`：仅接受 `type=="line_counting"`（`app_report_service.h:68`），且强制成员 `window/total/counter/target/model`——**Host 硬编码 Line Crossing 报表 schema**。
- **执行环境/契约**：报表内容由 App 生成（数据所有权在 App）；Host 职责为传输（MQTT/Webhook 双路冻结）+ 准入校验（防畸形内容外发）。当前校验=业务校验，越权持有业务 schema。
- **建议**：Host 通用化为"信封校验 + 类型/长度策略"（`schema_version/report_seq/device_id/boot_id` 保留；业务成员校验下沉给 App 或由 admission 声明报表类型注册表）。**放宽校验=更多载荷可外发，属产品/安全裁决，A 独占**。
- **障碍**：报表 ABI（v3 契约，冻结）；`tests/app_report` 向量绑定现 schema。
- **最小隔离适配**：把 `:1055-1104` 拆成 `report_envelope_check()`（通用）与 `report_business_schema_check()`（适配器）两个函数，行为不变。

### B4｜准入策略分支 `APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64`——【归类：改为 Host 通用服务（退役休眠分支，枚举值冻结）】

- **现状**：`app_admission.c:376-386` 特例分支（要求能力位组合 + `event_max/report_max` 下限）；`app_admission.h:92` 枚举值 1。
- **休眠证据**：生产三处安装策略全部选 `GENERIC_FULL`（`api_app_install_module.c:114`、`app_host_run.c:135`、`app_host_session.c:1629`）；`git grep -rn "BUSINESS_LINE_CROSSING" -- tests/` **零命中**。即该分支当前无任何生产/测试触发路径。
- **建议**：退役该分支（枚举值 1 保留——manifest/policy 协议值冻结；已发行包不因此重签）。**前置 UNKNOWN U7**：未逐字节核验全部历史 `.neapp` fixture 的 manifest 是否声明 profile=1（若某历史包声明且被重放，该分支仍有语义——A 决定是否保留兼容重放）。
- **验证**：`make -C tests/app_installer test` + `make -C tests/app_unsigned test` + 全 fixture 重放。

### B5｜控制消息 `APP_CONTROL_MSG_TYPE_COUNTERS_RESET` 与 bridge op `counters.reset`——【归类：因兼容必须暂留 Host】

- **现状**：`app_host_control.h:18-22`（消息类型值 1-5 冻结）；op 名见 `api_app_session_module.c:545`。名称计数味浓，但 Host 侧仅做不透明转发+回执（`app_host_control.c:1240-1311` snapshot 同为不透明载荷 + 版本元数据，`api_app_control_module.c:379-441` `cJSON_AddRawToObject` 原样透出）。
- **判定**：语义归 App（LC 计数器归零由 lc App 解释执行）；Host 保留协议值与通道。**不可改名**（协议值/外部接口冻结清单 F8）。

### B6｜`app_bridge_conf_host_permille()`（Host AI 阈值注入业务配置）——【归类：暂留 Host（平台服务），业务用途随 B1 消解】

- **现状**：`api_app_session_module.c:2376-2384` 读 `ai_get_confidence_threshold()`（Host AI 服务）换算 permille 注入 LC 默认配置。
- **判定**：NPU/AI 配置只存在于 Host（执行环境）；读取器是平台服务。其"LC 默认值注入"用途随 B1 迁移消解（App 需要时应经自身平台面获取——当前 App 无直接 AI 服务访问，迁移时需在 session 后端补只读访问器或由 UI 传入）。

### B7｜报表内容生成 / 越线计数规则 / 业务 UI——【已归 App，无需迁移（本轮核verified）】

- 规则/跟踪/计数引擎：仅存在于 ne30x-app `apps/line-crossing/{src,include}`（tracker/line_cross）；固件 `Custom/` 零命中（§0 扫描）。
- 报表内容生成：App 侧生成（lc App 组装 JSON），Host 仅校验（B3）+ 传输（`app_report_service_device.c` 通用 MQTT/Webhook 传输）。
- 业务 UI 特化：ne30x-app `apps/line-crossing/ui/`；Host 的 `/appsite/` 投影（`web_assets.c`）与 Web 前端 applicationManagement 为通用 App 管理面，无 LC 字段解释（`Web/src` 扫描仅含通用 sidecarBridge/授权逻辑）。

### B8｜AI/模型/视频对接（`app_host_session_device.c`）——【归类：因执行环境必须暂留 Host】

- `:13` `ai_service.h`、`:416-460` `ai_service_get_ai_node()`/`video_ai_node_add_result_listener`/`video_hub_subscribe`——NPU 结果与视频帧只能由 Host 喂给 App session（App 无 HAL 访问）；`dev_fill_model_view_from_json`（模型视图缓存）同为平台数据。数据所有权在 Host 平台层，**不迁移**。

### B9｜报表策略（`app_report_policy.c`）——【归类：Host 通用服务（现状已通用）】

- MQTT/Webhook 使能/配额的持久化与 rebase，无业务字段；随 B3 的类型注册表演进即可，本轮无需动作。

## 2. 三归类汇总（A 评论要求的结构）

| 归类 | 项 | 来源文件 | 消费者 | 建议归属 | Host 保留接口 | 迁移风险 |
|---|---|---|---|---|---|---|
| 迁往 App | B1 LC 配置编解码 | api_app_session_module.c:2228-2478 | bridge config.apply/read、counters.reset；UI ConfigPage；tests/web_sidecar_bridge | ne30x-app apps/line-crossing/app（lc_cfgcodec 已在） | op 路由+授权+长度/代数校验（opaque 透传） | 高（bridge 线协议+UI 契约+App control 通道三处同窗口） |
| 迁往 App | B2 target_class 投影 | api_app_session_module.c:1099-1123,1519-1654 | live bridge 载荷；UI VideoPreview | 随 B1（App 自带展示元数据） | live 传输（环/GAP/STALE）不变 | 中（live 载荷形状冻结，需协议演进窗口） |
| 改为 Host 通用服务 | B3 报表 schema 校验 | app_report_service.c:1055-1104 | 双路报告发送门；tests/app_report | Host 保留信封校验；业务 schema 下沉 App/注册表 | 信封校验+类型/长度策略 | 中-高（放宽校验=安全/产品裁决，A 独占） |
| 改为 Host 通用服务 | B4 休眠 profile 分支 | app_admission.c:376-386（枚举 :92） | 无生产/测试触发（已证） | 退役分支；枚举值冻结 | GENERIC_FULL/NONE 语义不变 | 低（前置 U7 fixture 核验） |
| 暂留 Host（执行环境） | B6 AI 阈值读取器 | api_app_session_module.c:2376-2384 | B1 默认配置注入 | 平台服务保留；LC 用途随 B1 消解 | `ai_get_confidence_threshold` 只读 | 低 |
| 暂留 Host（执行环境） | B8 AI/模型/视频对接 | app_host_session_device.c:13,400-470 | session 后端 | 不迁移 | session 设备后端全集 | — |
| 暂留 Host（兼容） | B5 counters.reset 通道 | app_host_control.h:18-22；session:545 | LC App 解释执行 | 协议值/通道冻结 | 消息类型值 1-5 不变 | — |
| 已归 App（核verified） | B7 规则/内容生成/业务 UI | ne30x-app apps/line-crossing/** | — | 无需迁移 | — | — |
| 已通用（无需动作） | B9 报表策略 | app_report_policy.c | session/report | 现状保留 | — | — |

## 3. 落地排序建议（供 A 立题）

1. **issue-H-B1a（隔离，零行为）**：抽取 B1+B2 为独立业务适配器翻译单元（`app_bridge_business_config.c`）+ B3 双函数拆分。验证：M2.4/M2.6 + 行为不变（无新参数、无路由变化）。
2. **issue-H-B4（休眠分支退役）**：前置 U7 fixture manifest 核验；枚举值保留。
3. **issue-H-B1b（真迁移，双仓窗口）**：Host opaque 透传 + ne30x-app control 通道收 JSON/透传 + UI 契约不变或同步演进；需 A 批准 bridge 协议演进窗口与 App 仓 PR 计划。
4. **issue-H-B3（报表类型注册表）**：A 先裁决放宽边界（哪些 type/成员集允许外发）。

## 4. 本节新增 UNKNOWN

| # | UNKNOWN | 影响 |
|---|---|---|
| U7 | 历史 `.neapp`/向量 fixture 的 manifest 是否声明 business profile=1（逐字节核验未做） | B4 退役安全性前置条件 |
| U8 | bridge `config.apply` op 的 UI 端字段全集与 Host 编码器字段集是否严格一致（双仓字段名 diff 未逐项跑） | B1 迁移的字段遗漏风险 |
| U9 | App 侧对 `counters.reset`/`enable` 的回执语义细节（属 App 仓内行为，本轮只核 Host 面） | 不影响 Host 判定，列档备查 |
