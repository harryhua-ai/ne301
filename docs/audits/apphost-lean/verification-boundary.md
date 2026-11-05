# AC4 — 后续实施与验证边界（Lean AppHost R0）

固件 @ `cfb90ed22ecf3cf750ce08799140c2789e38df27`；App @ `460c8cd17c45d04aedf1cde0af4a8123502d91d7`。

**分类口径**：`已证实`（本审计已用可重放命令验证）｜`可离线复现`（本地无硬件可执行，命令给出）｜`待CI·工具链`（需 ARM GCC/ST 工具链或 CI runner）｜`待真机授权`（需 A 授权设备操作，本审计一律未做）｜`UNKNOWN`。

**红线重申**：容量/性能目标仅指 HaLow 配置（`NETIF_WIFI_HALOW_IS_ENABLE=1`，经 `C_DEFS_EXTRA` 注入，`Appli/Makefile:914`；CI 主变体即此配置 `.github/workflows/main.yml:183`）。**不得**把 #172 历史约 270 KB 整树增量当作可删除 Host 字节（口径未复现，见 U2）；**不得**宣称 FPS 已改善（无任何实测）。文件移动 ≠ 瘦身/提速。

---

## M1. rename-only（矩阵组 R1/R2 + D2 文件名部分）最低验证

| # | 检查 | 命令/方法 | 分类 |
|---|---|---|---|
| M1.1 | rename 前后旧名零残留 | `git grep -n "<old_name>"`（含 tests/、docs/、Web/）= 0 命中 | 已证实可作门（当前旧名命中面已测：R1=0 外部引用、R2=8 include 者、D2=4 测试文件+1 Makefile 依赖行） |
| M1.2 | 固件全量构建不回退 | `make app`（APP_HOST_POC 默认 1，BOARD_PSRAM_SIZE=64；32MB 配置应依旧 `$(error)`，`Appli/Makefile:691`） | 待CI·工具链 |
| M1.3 | 受影响测试套 | R2：`make -C tests/app_host test`、`make -C tests/app_sidecar_lifecycle test`；D2：`make app-host-enable-test`（根 Makefile:197）+ `make -C tests/app_native_vnext test` + `make -C tests/app_unsigned test` | 可离线复现（host cc） |
| M1.4 | Web 前端不涉（rename 不触 Web/src） | `pnpm -C Web test`（防间接） | 可离线复现 |
| M1.5 | 双仓联合门（pin 历史 SHA）不受影响 | ne30x-app：`HOST_REPO=<本仓> tests/line-crossing-live/run_tests.sh`、`tests/line-crossing-enable/run_joint_matrix.sh`（历史 SHA git archive 自带旧路径，理论上不变；执行以证实） | 可离线复现（需两仓本地检出+历史对象存在） |

## M2. 解耦（矩阵组 D1/D3 + H1）最低验证

| # | 检查 | 命令/方法 | 分类 |
|---|---|---|---|
| M2.1 | D1 拆分后无重复符号、无丢符号 | `make -C Appli` 全链接（重复符号即链接失败）；`arm-none-eabi-nm` 对比拆分前后 4 个对象（installer/sidecar_verify/sidecar_lifecycle/pair_installer）符号集合差集=∅（允许局部符号差异）；13 个测试 Makefile 各自新增独立 .o 目标后逐套件链接 | 待CI·工具链 |
| M2.2 | D1 行为等价（重点：6 fn + 2 状态标志 + 1 宏/守卫的缝处理，见 d1-split-closure.md） | `make -C tests/app_installer test`（含 faults/product_version/install_gate）+ `make -C tests/app_native_vnext test` + `make -C tests/app_sidecar_lifecycle test` + `make -C tests/app_state test` + `python3 tests/app_platform_offline/run.py`（9 套件聚合）；增补断言：卸载收尾三语句（lifecycle.c:2809/2879/2880，g_boot_resolved/g_state_unknown）语义等价；守卫迁移后人为错配 `APP_INSTALL_PENDING_BIN_SIZE` 应 #error 编译失败 | 可离线复现 |
| M2.3 | D1 链接产物对比 | 拆分前后 `Appli/build/*.map` 中 AppHost 各对象 .text/.bss 聚合差 ≤ 链接器对齐噪声（记录数值，不设瘦身目标） | 待CI·工具链 |
| M2.4 | D3 拆分后 16 路由全回归 | `make -C tests/web_sidecar_bridge test`、`make -C tests/web_ota_gate test`、`make -C tests/web_sidecar_assets test`、`make -C tests/web_host_policy`；`curl` 冒烟（真机，待授权） | 离线套件可复现；HTTP 冒烟待真机授权 |
| M2.5 | D3 注册/初始化次序不变 | 代码审查锚点：`web_service.c:103-107` 注册顺序、`api_app_install_module.c:976-994`（storage_init→boot_resolve）不得移位/重排 | 已证实（次序现状已记录于 key-risks.md §3） |
| M2.6 | H1 表驱动改造 | `make -C tests/app_report test`、`make -C tests/app_report_uninstall test`、`make -C tests/report-policy-capacity-study`；ne30x-app `tests/line-crossing-live/run_tests.sh`（真实发送端联测） | 可离线复现 |
| M2.7 | OTA 前置停 App 不回退 | `make -C tests/web_ota_gate test`（覆盖 `api_ota_module.c:1360` 路径） | 可离线复现 |
| M2.8 | 业务适配器隔离/迁移（B1–B6，business-ownership.md） | 隔离期（零行为）：`make -C tests/web_sidecar_bridge test` + `make -C tests/web_sidecar_assets test` + `pnpm -C Web test`（appManagement 全套）+ bridge op 向量回归；迁移期（双仓）：ne30x-app `tests/line-crossing-runtime/run.sh`（离线 bridge 契约）+ `tests/line-crossing-live/run_tests.sh`（HOST_REPO 指向本仓）+ 真机 config.apply→snapshot 读回冒烟 | 隔离期可离线复现；迁移期双仓联测可离线复现（需两仓检出）；真机冒烟待真机授权 |

## M3. HaLow 固件容量（`NETIF_WIFI_HALOW_IS_ENABLE=1`）最低验证

| # | 检查 | 命令/方法 | 分类 |
|---|---|---|---|
| M3.1 | 基线 map 采集 | `C_DEFS_EXTRA="-DNETIF_WIFI_HALOW_IS_ENABLE=1" make app`，解析 `Appli/build/*.map`：AppHost 18+3 个对象各自 .text/.rodata/.bss/.data 聚合 → 建立**当前基线数字**（本次 R0 未采集 → UNKNOWN，非"已证实"） | 待CI·工具链（本地需 ARM GCC 13.3+） |
| M3.2 | PSRAM 内部布局不越界 | map 断言已内建：`__psram_bss_end__ <= ORIGIN(APP_HOST_RAM)`（ld:187）链接期自动检查；补人工检查 `APP_HOST_RAM` 2M 内 App 实际用量（App 仓 `lc-line-crossing-v4.elf` size，`make -C apps/line-crossing/app elf-v4 && arm-none-eabi-size`） | 待CI·工具链 |
| M3.3 | 变更前后对比口径 | 任何 D/H 实施issue 只允许引用 M3.1 口径的**对象级差值**；禁止引用 #172 的 ~270KB 整树数字作为可回收量（U2：口径未复现） | 已证实（口径规则，本行即规则） |
| M3.4 | Flash 分区占用不变式 | AppHost 不新增分区（LittleFS 目录 + NVS_USER key，`app_installer_storage.c`）；若实施引入新 key/目录，须在 issue 中给出 NVS 64KB/LittleFS 64MB 余量证明 | 已证实（现状无新增分区）；未来项 UNKNOWN |

## M4. 实时性能（HaLow 配置）最低验证

| # | 检查 | 命令/方法 | 分类 |
|---|---|---|---|
| M4.1 | 明确不宣称项 | **不宣称 FPS 改善/劣化结论**——R0 无任何真机测量；既有 Live/报告延迟结论只有离线性质（如下） | 已证实（=无可引用实测） |
| M4.2 | 离线可复现的时序性质 | Live GAP 判定逻辑：`make -C tests/app_host_live test`（含 joint，app27 fixture）；报告队列容量/等待布局：`make -C tests/report-policy-capacity-study`、`capacity_model.py`；报告 pump 周期常量（`APP_REPORT_DEV_*`，tests/app_report/Makefile:18-23）已有单测 | 可离线复现 |
| M4.3 | 真机授权后最低测量集（建议，非本次） | (a) App 运行中 `ai perf`/`sys tasks` CLI 采样（任务 CPU/栈）；(b) Live 轨迹端到端延迟（App 上报→`/api/v1/app/session/status` 可见）；(c) MQTT 与 Webhook 双路并发下的报告积压深度（`/api/v1/app/report/service` stats：`rejected_unauthorized` 等）；(d) OTA 前置 `app_host_session_stop(8000ms)` 实测耗时分布 | 待真机授权 |
| M4.4 | Session 停止超时边界 | `api_ota_module.c:1360` 8s 超时 + 拒绝升级路径：`tests/web_ota_gate` 覆盖拒绝分支；真机确认 8s 预算在 HaLow 满载下不误拒 | 离线部分可复现；真机部分待授权 |

---

## A 可直接立题的实施建议（独立 issue 粒度，按风险升序）

1. **issue-R（rename-only）**：执行矩阵组 R1+R2+D2 文件名（3 个最小 PR 粒度，各自可独立回退）。验证 = M1 全表。预期收益：命名一致、消除 "vnext" 代号；**不承诺任何字节/FPS 收益**。
2. **issue-D1（installer 编译单元拆分）**：删 `app_installer.c:1305-1307` 三连 include；按 `d1-split-closure.md` §2 最小接口方案（内部头承载 6 个双向 static 函数声明 + `APP_INSTALL_PEND_*` 宏链与 `#if/#error` 守卫整体迁移 + installer 提供状态标志访问器）；Appli/Makefile +3 编译单元；同步 13 个测试 Makefile 依赖行。验证 = M2.1-M2.3（含卸载收尾语义等价与守卫失效注入断言）+ M3.1 前后 map。与 #180（Deferred 安装恢复缺陷）排程解耦，避免同文件冲突。
3. **issue-D3（Web session 模块拆分）**：api_app_session_module.c → session/report/sidecar_bridge 三模块；保持注册顺序与初始化锚点（key-risks.md §3）。验证 = M2.4-M2.7。
4. **issue-H1（报告类型表驱动 = business-ownership.md B3）**：需 A 先裁决注册面（manifest 声明 vs 编译期表）与放宽边界；冻结 `"line_counting"` 值与双路语义。验证 = M2.6。建议先做零行为隔离（信封/业务 schema 双函数拆分）。
4b. **issue-H-B1a / H-B4（业务隔离与休眠分支退役，见 business-ownership.md §3）**：H-B1a 把 `api_app_session_module.c:2228-2478`+`:1099-1123` 抽为独立业务适配器翻译单元（零行为）；H-B4 退役休眠 profile 分支（`v3_business_profile` 为 Host 调用方策略字段、包 manifest 无此字段；前置=U7 重构口径的 16 处赋值闭合 grep 门固化，fixture 回归为常规回归项）。验证 = M2.4 + `make -C tests/web_sidecar_bridge` + fixture 全回归。
4c. **issue-H-B1b（真迁移，双仓窗口）**：Host opaque 透传 + ne30x-app control 通道/UI 同步演进；需 A 批准 bridge 协议演进窗口与 App 仓 PR 计划；验证 = 双仓联测（ne30x-app tests/line-crossing-runtime + HOST_REPO 联合脚本）+ 真机 config.apply→snapshot 冒烟（待授权）。
5. **issue-A1/A2/A3（裁决类）**：旧 Run/ABI 1.0 淘汰与否（A 独占）；`app_native_vnext_floor_*` 符号改名（需与 ne30x-app pin 更新同窗口）；AppHost 目录名保留结论确认。本审计只提供消费者证据，不预设立场。
6. **issue-V（验证基建，可选）**：把 `app-platform-offline`（改名后）9 套件聚合接入固件 CI（当前 main.yml 不跑 tests/，`M*` 全部停在离线/工具链级）。这是把本文件"可离线复现"升级为"CI 已证实"的唯一路径。

## 残余风险（实施期仍需警惕）

- R-1 双仓协调：任何触及 `Custom/Services/AppHost` 路径或 floor 符号的实施，更新 ne30x-app 联合 pin 时必须同窗口（migration-matrix F3）。
- R-2 fixture 证据链：F2 行全部 fixture 不得 rename/覆盖；生成器再生成须哈希一致（U4）。
- R-3 持久化状态：NVS key 与 pending blob 编码是兼容面（key-risks §2），D1 虽"行为等价"，boot_resolve 路径必须全 fault 套件回归。
- R-4 文档漂移：`docs/web-api/endpoints/*` 疑为手工维护（U6），路由类实施需同步核对。
- R-5 容量口径：在 M3.1 基线建立前，任何"省了多少"的说法都不成立；270KB 历史数字仅作线索（U2）。
- R-6 发布对齐：固件↔App 官方对照对的完整清单未证（U3）；实施期如需引用"已验证组合"，先由 A 确认发布对。
