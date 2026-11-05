# AC3 — 关键风险复核（Lean AppHost R0）

固件 @ `cfb90ed22ecf3cf750ce08799140c2789e38df27`；App @ `460c8cd17c45d04aedf1cde0af4a8123502d91d7`。
本文件只**记录**现状与门槛，不提议改变任何冻结语义；裁决权在 A。

---

## 1. `app_installer.c` 直接 include 其他 `.c` 的跨编译单元耦合

**事实链（可重放）**：

```sh
git grep -n '#include.*"\(.*\)\.c"' -- Custom/ | grep -v "Common/Lib"
# app_installer.c:1305 #include "app_sidecar_verify.c"
# app_installer.c:1306 #include "app_sidecar_lifecycle.c"
# app_installer.c:1307 #include "app_pair_installer.c"
```

- 合并编译单元 ≈ 1307+3679+626+523 = 6,135 行。`Appli/Makefile:693` 只编译 `app_installer.c` 一个对象。
- **静态函数跨文件缝（本次用脚本精确测定）**：三个被 include 的 `.c` 共 110 个 `static` 函数；其中**仅 2 个**被 `app_installer.c` 引用——`inst_sidecar_revoke_for_native`、`inst_uninstall_intent_blocks_boot`：`app_installer.c:47,50` 是前向声明（`static`），实现实体位于被 include 的 `app_sidecar_lifecycle.c` 内，靠 TU 拼接才成立。拆分编译单元时**必须且只需**把这 2 个函数提升到一个内部头（或去掉 static 经内部头声明），其余 108 个 static 无跨文件引用。
- 附带效应：合并单元使 `-Wall` 检查/内联/静态分析都发生在 6K 行尺度；`tests/app_installer/Makefile:119`、`tests/app_native_vnext/Makefile:39`、`tests/app_sidecar_lifecycle/Makefile:47`、`tests/app_host_control/Makefile:99` 把这些 `.c` 登记为 `app_installer.o` 依赖（共 13 个 Makefile 编译 app_installer.c）。
- 与 #180（安装恢复缺陷，Deferred）的关系：#180 属修复线，本审计不触碰其行为；但 D1 拆分 PR 若与 #180 修复并行，需 A 排序避免冲突。

## 2. Installer / Sidecar 事务恢复

- 安装事务 = LittleFS 文件 + `NVS_USER` KV 双介质（`app_installer_storage.c:24-150`：`flash_lfs_*` + `storage_nvs_*`）。
- 恢复协议（`app_installer.c`）：`APP_INSTALL_KEY_PENDING` pending blob（`:228-244,811-814,933-935` 编码 INSTALL/UNINSTALL intent + prev record）；`app_install_boot_resolve()`（`:1148-1216`）在 Web 注册时执行（`api_app_install_module.c:988-994`），失败则 fail-closed 并"deferred to first API use"（`app_install_boot_resolve_reset`）；每个入口在 `g_boot_resolved != TRUE` 时先 resolve（`:666,910,1006,1122`）。
- Sidecar 层：`sc_store_stage_recover_locked`（`app_sidecar_lifecycle.c:1199`）恢复 stage 槽；`app_native_vnext_floor_recover`（`:2627`）在 boot resolve 第一步调用（`app_installer.c:1170`）；slot/candidate 清理路径 `:1918-1933,2991,3316-3355`。
- 风险点（记录）：(a) boot resolve 失败仅记 ERROR 不阻断启动——设备可带未决 intent 运行，首次 API 使用时再 fail-closed（当前语义，冻结）；(b) NVS key（`app_native_vnext_floor`/`app_native_vnext_epoch`/pending key）是持久化状态 → **任何 rename 都是兼容变更**；(c) 事务正确性证据在 `tests/app_installer/test_app_installer_faults.c`、`tests/app_state/test_app_state_faults.c`、`tests/app_sidecar_lifecycle`（golden 恢复向量）——D1/H1 实施必须跑全。

## 3. Web Session / Bridge / Report 权限与注册

- 注册链（唯一入口）：`web_service.c:103-107` `#if NE301_APP_HOST_POC` 依序 `web_api_register_{app_install,app_session,app_control}_module()`。
- `web_api_register_app_install_module()`（`api_app_install_module.c:965-996`）承担**运行时初始化**：`app_host_run_init`（旧 Run 后端）→ `app_installer_storage_init`（LittleFS/NVS）→ 4 路由注册 → `app_install_boot_resolve`。**AppHost 的存储初始化与事务恢复挂在 Web 模块注册上**——拆分 Web 模块（D3）时此初始化次序必须保持（先于任何 session/bridge 路由可达）。
- 权限模型（冻结）：全部 16 条 `/api/v1/app/*` 路由 `require_auth = AICAM_TRUE`；处理器内再显式 `ctx->authenticated != AICAM_TRUE` 拒绝（install `:207`、uninstall `:746`、run `:857`、session start/stop/status `:175,252,314`、report `:406`、sidecar ui `:1930`）——文案统一 "high-privilege admin/authentication required"。
- Bridge 授权（session 模块 `:558-1030`）：grant 槽 + confirm token 槽（`g_app_bridge_grants/g_app_bridge_confirms`）、`grant_id` 一次性消费（`app_bridge_consume_confirm:893`）、危险操作 confirm 流（`/confirm`、TTL/重置 `app_bridge_grants_reset:808`）；对应前端 `Web/src/services/sidecarBridge.ts`（`SIDECAR_GRANT_REFRESH_MARGIN_MS=60_000`、`SIDECAR_DANGEROUS_CONFIRM_TTL_MS=120_000`）与 `Web/src/test/appManagement/sidecarBridgeConfirmFlow.test.ts`。
- 入口 Host 分类（web_server.c:855-881）：fail-closed——未知/畸形 Host 403；`WEB_ENTRY_APP_HOST_API` 仅当 admin 监听 + `GET/HEAD /appsite/*` + `web_sidecar_projection_same_origin_allowed()` 才放行；跨站写直接 403 `cross-site forbidden`；无 Origin 的副作用请求被拒（注释明示"authorized non-browser tools must send a same-site Origin header"）。策略层测试 `tests/web_host_policy`。
- 风险点：D3 拆分不得改变注册顺序/认证判定次序；`web_assets.c:214` `/appsite/g` 投影与 `web_server.c` sidecar handler 的 `route_generation` 一致性逻辑跨两个文件。

## 4. 旧 Run / ABI 的实际消费者（A 独占裁决，本节只列证据）

- Run 路径：`app_host_run.c/.h`（`app_host_run.h:8` include `app_host_abi.h`，`APP_HOST_ABI_VERSION=0x00010000`）+ `app_host_run_device.c`（`:47` `entry(api, APP_HOST_ABI_VERSION)`）。session 路径的设备后端独立（`app_host_session_device.c`）。
- **消费者（在用，非死代码）**：
  - `POST /api/v1/app/run`（`api_app_install_module.c:857-963`，调 `app_host_run_installed:913`）；
  - `app_host_run_init` 在模块注册时无条件执行（`:974`）；
  - OTA 升级前置停 App 用的是 **session** 停止（`api_ota_module.c:1360` `app_host_session_stop`），不是 Run——两路径在 OTA 场景已收敛到 session；
  - 测试：`tests/app_host_run`、`tests/app_installer/test_app_install_gate.c:12`（include `app_host_run.h`）、`tests/app-platform-offline/run.py` 聚合包含 `tests/app_host_run`；
  - `app_host_abi.h` 的 8 个 include 者（dependency-closure.md §2.1）。
- 结论性事实（不裁决）：Run 与 Session 并存且都被生产路由触发；`/api/v1/app/run` 是已发布外部接口。

## 5. Native 入口 / 布局

- 执行区：`STM32N657L0HXQ_LRUN.ld:22` `APP_HOST_RAM 0x93E00000 +2M`（唯一 ld；32MB/DEBUG ld 无此区 → AppHost 构建被 PSRAM=64 硬门挡住，`Appli/Makefile:686-712` `$(error)`）。
- App 侧：`lc_app.ld` `APP(rx) 0x93E00000 2M`、`ENTRY(app_entry)`、**`ASSERT(SIZEOF(.data)==0,"...host loader does not initialize it")`** → 镜像不得含初始化数据；`app/Makefile:21` `EXEC_BASE ?= 0x93E00000`、`-ffreestanding -nostdlib -lgcc`。
- 入口序：`app_host_session.c:1378-1394` 按 `g_container_major==5 → ABI V4` 否则降级选表，`invoke_entry(api, abi_version,…)`；ABI 常量 `APP_ADMISSION_ABI_VERSION_V2/V3/V4 = 0x0002/3/4_0000`（`app_admission.h:39-41`）；准入强校验 `app_admission.c:145,336-358`（manifest 与 native 头双向一致）。
- App 仓打包：`neapp_v5_native.py:14-19` `NATIVE_MAGIC=b"NEA1"`、`HEADER 32`、`HOST_ABI_V4=0x00040000`；`neapp_v5_format.py:598-603` 校验 `native target_addr+file_len` 必须落在 Host 执行区。
- 风险点：地址/长度是双仓硬约束（固件 ld ↔ app ld/Makefile/打包校验三点同步）；`.data==0` 约束意味着 Host 侧任何"帮 App 初始化数据"的改动都是行为变更。

## 6. NEAPP ELF 输出与发布 provenance 兼容门槛

- 产物链（App 仓）：`make elf-v4`（`lc-line-crossing-v4.elf` → payload）→ `make neapp-v5`（`neapp_pack_v5.py`）→ `line-crossing-<PRODUCT_VERSION>.neapp` + `.verification.json`（`build_neapp.sh`，验证 `--host-caps-hex 0x7ff` 模拟 CAP_PRODUCT_VERSION 0x400 的 Host）→ `release_tool.py` 拒绝非 PASS verification / 缺 `package.sha256`、`app_id`、`native_version`、`ui_version`、`source_commit`、`product_version`（MAJOR≥1）→ GitHub `release-app.yml`（main 手动 dispatch）。
- provenance 记录（`tests/fixtures/line-crossing-4.0.0.neapp.provenance.json`）：`format:"neapp-v5-unsigned"`、`generator:"neapp_pack_v5.py 5.0.0"`、`native_build.target_addr=2480930816(0x93E00000)`、`elf_entry=+1(Thumb)`、`entry_offset=0`、toolchain 指纹（arm-none-eabi-gcc 15.2.1）、逐资产 sha256。
- 固件侧验证对称性：`app_admission.c` 验 manifest/native 双头一致 + ECDSA（`tests/sign_spike` 提供密钥 spike）；fixture `line-crossing-1.0.1/4.2.12.neapp` 由 `make_product_version_fixtures.py` 再生成（U4：1.0.1 来源待证）。
- 兼容门槛（冻结）：`"NEA1"`、32B 头、format 1、`HOST_ABI_V4`、v5 容器内 native=v4、capability 位（含 `CAP_PRODUCT_VERSION 0x400`，`app_admission.h:72-75`）、verification JSON 字段集、provenance 字段集。**任何 Host 侧改名/重排都不得改变上述任何字节级或 JSON 字段级对外形状。**
- 发布渠道事实：固件 CI（main.yml）只构建+发布固件件，不跑 tests/；App 仓 CI（neapp-unsigned.yml）在 PR/push 全量验证 unsigned 包。

## 7. 冻结语义清单（只记录，不提议更改）

| # | 冻结项 | 锚点 |
|---|---|---|
| F1 | State/Control 语义（config/reset/status、revision/generation） | `app_host_control.c/.h`；`/api/v1/app/control/*` 3 路由 |
| F2 | Live/Tracks 语义（包序/gap 判定） | `app_host_live.c/.h`；`/app/session/status` 的 live 段 |
| F3 | GAP/STALE 判定（`APP_HOST_LIVE_STALE_MS=10000`、`APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA`、`FRESHNESS_STALE/GAPPED` 枚举值） | `app_host_live.h:31-70,121` |
| F4 | MQTT 与 Webhook **两路独立**报告（独立 state/attempts/wait/outcome，各自 pending 判定） | `app_report_service.c:46-78,349-350` |
| F5 | 同源 `/appsite/`（fail-closed Host 分类、GET/HEAD-only、same-origin 门） | `web_server.c:855-930` |
| F6 | 管理授权（16 路由 require_auth + 处理器内高权限管理员复核） | §3 |
| F7 | 单 App 兼容（pair history：release_seq 单调、降级/冲突拒绝；installed 单槽） | `app_pair_history.h:26-45`；`app_pair_installer.c` |
| F8 | 协议值/外部接口/已发布包/历史 fixture/pin SHA | migration-matrix.csv F1–F3 行 |

## 8. 风险分级汇总（面向 A 的实施排序参考）

| 级 | 项 | 说明 |
|---|---|---|
| 高 | 触碰 F1–F8 任何一项 | 均为对外承诺或持久化状态；需要 A 明确授权 + 独立 issue |
| 高 | D3（Web 模块拆分） | 注册/初始化次序与认证判定次序耦合（§3）；收益是可读性，不是瘦身 |
| 中 | D1（installer 拆分） | 缝已测清（仅 2 个 static 跨文件）；13 个 Makefile 机械同步；行为等价需 map 对比佐证 |
| 中 | D2（vnext 头改名） | 文件名闭合；NVS key 冻结；符号改名另行裁决（A2） |
| 低 | R1/R2、H2（目录/样例 relocate） | 零外部引用已证实 |
