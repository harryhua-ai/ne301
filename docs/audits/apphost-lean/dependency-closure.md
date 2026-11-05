# AC1 — 双仓第一方消费者闭合（Lean AppHost R0）

固件源：`camthink-ai/ne301:app-host@cfb90ed22ecf3cf750ce08799140c2789e38df27`
App 源：`camthink-ai/ne30x-app:main@460c8cd17c45d04aedf1cde0af4a8123502d91d7`
所有命令在固件仓 worktree（上述 SHA 检出）执行；App 侧命令在 `/Users/harryhua/n301-dispatch/appsrc-460c8cd1`（同 SHA 只读检出）执行。除特别注明外命令均可原样重放。

---

## 0. 扫描方法与排除项

主扫描命令模板（固件仓）：

```sh
git rev-parse HEAD                                   # == cfb90ed22...（复核）
git ls-files Custom/Services/AppHost/                # 36 个文件清单
git grep -n "#include \"app_host.h\"" -- '*.c' '*.h' # 单头文件消费者枚举（逐头重复）
git grep -ln "Services/AppHost" -- ':!Custom/Services/AppHost'   # 字面路径引用
git grep -n '#include.*"\(.*\)\.c"' -- Custom/Services/          # 直接 .c include
rg -n 'app_native_vnext_floor_raise[[:space:]]*\(' --glob '*.c' --glob '!**/tests/**'  # App 仓联合门也在用
```

**扫描排除项（固件仓，均为有意排除并给出理由）**：

1. `Custom/Common/Lib/**`（lwip/mmx108/hostap/MbedTLS/SiliconLabs_SDK 等第三方库）：其中存在大量 `#include "*.c"`（lwip sockets.c、hostap crypto 等）与 `installer` 字样，与 AppHost 无关；用 `git grep -- 'Custom/' ':!Custom/Common/Lib'` 收窄后 AppHost 命中仅 3 处（见 §2.3）。
2. `Docs/`、`docs/`：v4.3.1 历史发布文档与 VitePress 站点；AppHost 相关仅 `docs/web-api/endpoints/app_{install,session,control}.md` 与 `manifest.json`（见 §3.7），其余排除。
3. `tests/pp/`：AI 后处理 `pp_name_type` 单元测试（模型名→类型表），与 AppHost 无关，排除。
4. `Hardware/`、`Model/`、`BSP/`、`FSBL/`、`WakeCore/`、`Middlewares/`：`git grep -il "apphost\|app_host\|appsite\|sidecar\|neapp"` 无命中（FSBL/ld 亦无 `APP_HOST_RAM`，见 §3.3），排除。
5. Web 前端 `node_modules/`、构建产物：仅扫 `git ls-files Web/src`。
6. App 仓 `apps/line-crossing/ui/` 第三方捆绑（jmuxer.min.js 等）：仅列清单，不逐一闭合。

App 仓排除项：`tests/line-crossing-ui/browser-runner/`（npm 打包件）、`*.neapp`/`*.neui` 二进制 fixture 内容本身（只登记文件名与哈希用途）。

---

## 1. 固件仓：AppHost 文件清单与职责（36 文件，SHA cfb90ed22）

`git ls-files Custom/Services/AppHost/`（排序后 36 项）+ `wc -l`：

| 文件 | 行数 | 职责（据头文件/实现静态阅读） | 直编编译单元 |
|---|---|---|---|
| app_admission.c/.h | 531 | NEAPP 包准入校验（manifest/能力位/ABI 版本 V2–V4、v5 容器）、ECDSA+SHA256 验签入口 | Appli 直编 |
| app_host.h | — | AppHost 公共门面（错误码、结果字符串） | 头 |
| app_host_control.c/.h | 1492/204 | App Control 通道（config/reset/status、修订号 generation） | Appli 直编 |
| app_host_live.c/.h | 855/225 | Live/Tracks 轨迹环与 GAP/STALE 判定（APP_HOST_LIVE_STALE_MS=10000） | Appli 直编 |
| app_host_region.c | 12 | 暴露链接符号 `__app_host_ram_base__/__app_host_ram_end__` 为 exec region | Appli 直编 |
| app_host_run.c/.h + app_host_run_device.c | 408+84 | **旧 Run 路径**（ABI 1.0 `APP_HOST_ABI_VERSION=0x00010000`，`app_host_run.h:8`） | Appli 直编 |
| app_host_session.c/.h | 2304/318 | Session 容器（v2/v3/v4 表、entry invoke、boot/pair 装配） | Appli 直编 |
| app_host_session_device.c/.h | 488 | Session 设备后端（跳转 entry、tick/log） | Appli 直编 |
| app_host_validate.c/.h | ~90 | 安装记录编码/解码校验 | Appli 直编 |
| app_installer.c/.h | 1307/199 | 安装/卸载事务（pending blob + boot_resolve）、**尾部 #include 三个 .c** | Appli 直编 |
| app_installer_storage.c | 240 | LittleFS 目录 + `storage_nvs_*`（NVS_USER）KV 后端 | Appli 直编 |
| app_native_vnext_floor.h | 55 | "native vnext floor"（NVS key `app_native_vnext_floor`/`_epoch`、MAJOR=4）；实现位于 app_sidecar_lifecycle.c | 头（实现被并入 app_installer.o） |
| app_pair_history.h | 47 | 配对历史（release_seq、降级/冲突判定）→ 单 App 兼容 | 头（实现在 app_pair_installer.c） |
| app_pair_installer.c/.h | 523 | 原生+UI 配对安装（pair TBS 验签、原子切换） | **经 app_installer.c:1307 并入** |
| app_report_policy.c/.h | 522 | 报告策略（MQTT/Webhook 开关与配额的持久化+rebase） | Appli 直编 |
| app_report_service.c/.h | 2559 | 双路报告队列（MQTT 与 Webhook 各自 state/attempts/wait） | Appli 直编 |
| app_report_service_device.c/.h | 1270 | 设备侧报告传输（MQTT/Webhook 客户端适配 + CLI 命令注册） | Appli 直编 |
| app_sidecar_lifecycle.c/.h | 3679 | Sidecar 生命周期（install/pair/query/boot_resolve、stage 恢复、native floor 实现） | **经 app_installer.c:1306 并入** |
| app_sidecar_verify.c/.h | 626 | UI(.neui)+native(.neapp) 验签 | **经 app_installer.c:1305 并入** |
| app_state_store.c/.h | 966 | App 状态存储（state blob、fault 注入点） | Appli 直编 |

合计 20,050 行 / 716,968 字节。复算：`git ls-files Custom/Services/AppHost/ | xargs wc -l | tail -1`。

---

## 2. 固件仓：消费者闭合（逐消费通道）

### 2.1 C `#include` 消费者（生产代码，AppHost 目录之外）

扫描：对 18 个头逐一 `git grep -n "#include \"<header>\"" -- '*.c' '*.h' | grep -v "^Custom/Services/AppHost/"`。结果（生产代码全集，无遗漏命中）：

| 消费者 | include 的 AppHost 头 | 用途 |
|---|---|---|
| `Custom/Services/Web/api/api_app_install_module.c/.h` | app_host.h, app_host_run.h, app_host_session.h, app_host_abi.h, app_admission.h, app_host_control.h, app_host_live.h, app_installer.h, app_pair_installer.h, app_report_service.h, app_sidecar_lifecycle.h | install/uninstall/run 路由 + `app_host_run_init`/`app_installer_storage_init`/`app_install_boot_resolve`（`api_app_install_module.c:965-996`） |
| `Custom/Services/Web/api/api_app_session_module.c` | app_host.h, app_host_session.h, app_host_session_device.h, app_admission.h, app_host_control.h, app_host_live.h, app_installer.h, app_pair_installer.h, app_report_policy.h, app_report_service.h, app_report_service_device.h, app_sidecar_lifecycle.h | session/report/sidecar UI/bridge 路由（16 条中的 12 条） |
| `Custom/Services/Web/api/api_app_control_module.c` | app_host_control.h | control 3 条路由 |
| `Custom/Services/Web/api/api_ota_module.c` | app_host_session.h（`:27`） | OTA 前置停 App：`app_host_session_stop(8000u, &out)`（`api_ota_module.c:1360`） |
| `Custom/Services/Web/web_assets.c` | app_installer.h, app_sidecar_lifecycle.h, app_pair_installer.h（`:15-17`，`#ifdef NE301_APP_HOST_POC`） | `/appsite/g` 资产投影（`web_assets.c:214`） |
| `Custom/Services/Web/web_server.c` | app_sidecar_lifecycle.h（`:18`） | Host 分类 + `/appsite/` 同源门（`:855-930`、`:1230-` sidecar handler） |
| `Custom/Services/Web/web_service.c` | （间接）三个 api_app_*.h（`:29-31`） | `#if NE301_APP_HOST_POC` 注册三模块（`web_service.c:103-107`） |
| `Custom/Core/Log/cli_cmd.c` | app_report_service_device.h（`:29`） | `app_report_service_device_cmd_register()`（`:1276`，`#if NE301_APP_HOST_POC`） |

`Custom/Common/Inc/app_host_abi.h`（ABI 1.0 Run 表）消费者：app_admission.c、app_host_run.h、app_host_run_device.c、app_host_session.h、app_host_validate.h、api_app_install_module.c、tests/app_host/test_app_host_validate.c、tests/app_sidecar_lifecycle/test_app_sidecar_lifecycle.c（`git grep -n "app_host_abi.h"`）。

> 业务归属增补（合同 2026-11-05 修订）：`api_app_session_module.c` 内 `:2228-2478` 还嵌入 Line Crossing 业务配置编解码块（144B "LCC3" 0x3343434C，与 ne30x-app `lc_cfgcodec.h` 双实现同构）及 `:1099-1123` 的 `target_class_name` 业务字段解析；逐项归属判定见 `business-ownership.md`（B1–B9）。

### 2.2 符号级消费者（无本地 include 的直接调用）

`git grep -nE "app_host_(session|run|live|control)_[a-z_]+\(" -- 'Custom/' ':!Custom/Services/AppHost'`：生产代码仅 `api_ota_module.c:1360` 一处（其余经由上述 include 就地的 Web api 模块调用）。即 AppHost 对生产代码的符号面完全收敛在 7 个 Web/CLI 文件内。

### 2.3 直接 `.c` include（跨编译单元耦合，AC3 关键项）

```sh
git grep -n '#include.*"\(.*\)\.c"' -- Custom/ | grep -v "Common/Lib"
# Custom/Services/AppHost/app_installer.c:1305:#include "app_sidecar_verify.c"
# Custom/Services/AppHost/app_installer.c:1306:#include "app_sidecar_lifecycle.c"
# Custom/Services/AppHost/app_installer.c:1307:#include "app_pair_installer.c"
```

结果：`app_installer.o` 是一个 6,135 行的合并编译单元（1307+3679+626+523）。13 个测试 Makefile 把这三个 `.c` 登记为 `app_installer.o` 的依赖（§2.5）。

### 2.4 Make 目标 / 产物 / 条件编译

- `Appli/Makefile:686-712`（`APP_HOST_POC ?= 1`，`ifeq BOARD_PSRAM_SIZE=64` 才编，否则 `$(error)`）：15 个直编 `.c` + 3 个 Web api 模块 `.c`；`Appli/Makefile:762` 条件加 `-I../Custom/Services/AppHost`；`Appli/Makefile:689` `-DNE301_APP_HOST_POC=1`。
- `NE301_APP_HOST_POC` 全部使用点：`git grep -n "NE301_APP_HOST_POC" -- '*.c' '*.h'` → cli_cmd.c:28/1275、web_assets.c:14/213（+web_assets.h:57）、web_server.c:17/923/1230、web_service.c:28/103、Appli/Makefile:689。
- 链接：`Appli/STM32N657L0HXQ_LRUN.ld:22` `APP_HOST_RAM (xrw): ORIGIN=0x93E00000, LENGTH=2M`；`:185-187` 导出 `__app_host_ram_base__/__app_host_ram_end__` 并断言不与 `.psram_bss` 重叠。**仅此一个 ld 含 APP_HOST_RAM**（`STM32N657L0HXQ_LRUN_32MB.ld`、`STM32N657L0HXQ_LRUN_RAM_DEBUG.ld` 均无 → 32MB/DEBUG 构建无法链接 AppHost）。ld 选择逻辑 `Appli/Makefile:928-934`。
- 根 `Makefile:192-198`：`app-native-vnext-test`、`app-host-enable-test` 两个 target（`$(MAKE) -C tests/... CC=cc test`）。这是根 Make 中唯二的 AppHost 钩子；`make/pkg/flash/sign` 不触碰 AppHost。
- `Script/`：`git grep -ln "app_host\|AppHost\|neapp\|sidecar" -- Script/` → **零命中**（NEAPP 打包器不在固件仓，见 §4）。
- `.github/workflows/main.yml`：仅构建+发布产物，**不运行任何 tests/**（对 §AC4 分类有影响：测试门目前全部为本地/离线）。

### 2.5 测试（固件仓 tests/）

26 个直接相关目录（`git ls-files tests/ | awk -F/ '{print $2}' | sort -u | grep -vE '^(pp|mem_pool_guard|threading_gate)$' | wc -l`，工作树 4,092 KB）+ 3 个相邻护栏目录（mem_pool_guard/threading_gate/nvs_credential_recovery，间接消费者，一并列入下表）：

| 目录 | 文件数 | 编译的 AppHost 单元 / 耦合 |
|---|---|---|
| tests/app_host | 5 | app_host_validate.c（Makefile:10） |
| tests/app_host_control | 5 | app_host_control.o + app_host_session.o + app_installer.o + app_admission.o + app_report_service.o + app_report_policy.o（Makefile:46） |
| tests/app_host_enable | 3 | `run_app30_joint.sh`：`APP_REPO`/`APP_SHA=85e3d4a46b8b4c53a63fac3405b91efb084a3f8a`（pin 到 ne30x-app 历史提交） |
| tests/app_host_live | 4 | app_host_live.o；`app27_live_fixtures.h`（历史 fixture） |
| tests/app_host_run | 2 | 旧 Run 路径 |
| tests/app_host_session | 51 | session 全链；内嵌 `app27_r3/`（历史 v2/v3/v4 ABI fixture app，**不可改名**）；devshim/ |
| tests/app_installer | 12 | installer 全套 + fixtures `line-crossing-1.0.1.neapp`、`line-crossing-4.2.12.neapp`（**已发布包 fixture，不可改名**） |
| tests/app_native_vnext | 8 | v5 容器/floor；fixtures `*.neapp.v4` |
| tests/app_report / app_report_uninstall | 5 | report service/policy |
| tests/app_sidecar | 2 | 生成器 gen_sidecar_fixtures.py |
| tests/app_sidecar_lifecycle | 8 | app_installer.o（合并单元，Makefile:47 列全部 4 个 .c）+ `golden.neapp.v3`/`golden.neui`/`test-publisher.spki.der` |
| tests/app_state | 4 | app_state_store |
| tests/app_unsigned | 5 | unsigned 准入 + 版本边界 |
| tests/app-platform-offline | 4 | 聚合器 run.py（串 9 个 suite：app_unsigned/app_installer/app_host_run/app_host_session/app_host_control/app_sidecar_lifecycle/app_state/app_report/storage_gate） |
| tests/counting_sidecar | 17 | **业务样例**：build-fixture.mjs:52 直接编译 `Custom/Services/AppHost/app_admission.c` |
| tests/line-counting-bridge-study | 3 | 研究 harness（app_window_probe/host_report_bridge） |
| tests/report-policy-capacity-study | 3 | 队列容量布局（app_installer.c 也在编译列表） |
| tests/storage_gate | 36 | 认证/存储门（shim 全集；不直接编译 AppHost .c，但 shim 面向其依赖） |
| tests/web_ota_gate | 6 | api_ota_module + `app_host_session.h`（test_web_ota_gate.c:9）；含 mqtt_service/ai_service shim |
| tests/web_host_policy | 5 | web_host_policy（/appsite 同源门的策略层） |
| tests/web_sidecar_assets | 3 | web_assets /appsite 投影 |
| tests/web_sidecar_bridge | 3 | bridge 路由 + app_pair_history.h |
| tests/sidecar_host_bridge | 13 | 浏览器端 bridge（fixtures `line-crossing.neui` + provenance，pin ne30x-app `1f48bdd5...`） |
| tests/mem_pool_guard, tests/threading_gate, tests/nvs_credential_recovery | 5+5+2 | AppHost 相邻护栏（线程/内存池/NVS 凭据恢复），列入间接消费者 |
| tests/sign_spike | 4 | ECDSA 验签 spike（app_installer 验签依赖面） |

字面路径引用 `Custom/Services/AppHost` 的文件全集（19 个，rename 的第一冲击面）：
`git grep -ln "Services/AppHost" -- ':!Custom/Services/AppHost'`
→ Appli/Makefile、tests/{app_host,app_host_control,app_host_live,app_host_run,app_host_session,app_installer,app_native_vnext,app_report,app_report_uninstall,app_sidecar_lifecycle,app_state,app_unsigned,report-policy-capacity-study,web_ota_gate,web_sidecar_assets,web_sidecar_bridge}/Makefile、tests/app_host_enable/run_app30_joint.sh、tests/counting_sidecar/build-fixture.mjs。
其中**编译 `app_installer.c` 的 Makefile 13 个**：`grep -rln "app_installer\.c" tests/*/Makefile` → app_host_run / app_host_control / app_installer / app_native_vnext / app_report / app_sidecar_lifecycle / app_host_session / app_unsigned / report-policy-capacity-study / app_report_uninstall / web_sidecar_assets / app_state / web_sidecar_bridge。

### 2.6 Web 前端（固件仓 Web/）

`git grep -li "sidecar\|appsite\|neapp" -- Web/src`（22 文件）：
- 路由：`Web/src/router/index.tsx:36` `/application-management`。
- 页面：`Web/src/pages/applicationManagement/{index,appDialog,appSlot,sidecarAppFrame}.tsx`。
- 服务：`Web/src/services/api/appManagement.ts`（端点 `/api/v1/app/installed|install|run|session/start|session/stop|sidecar/ui|sidecar/bridge(/grant|/confirm|/revoke)`，`:54-81`）；`Web/src/services/sidecarBridge.ts`（envelope 校验、grant/revoke 消息、VideoSessionTracker、isCrossSite）。
- 状态/类型：`store/appManagement.ts`、`types/sidecar.ts`；i18n：`locales/{en,zh}/sys.json`。
- 测试：`Web/src/test/appManagement/*`（10 个，含 siteSchemeMatrix/sidecarBridgeLive/ConfirmFlow/sidecarSlot/panelIsolation 等）。
- 转换链（`Frontend/static_to_c_converter.js` → `pack_web_assets`）不含 AppHost 字面引用（`git grep -ln "appsite\|sidecar" -- Frontend/` 零命中）。

### 2.7 固件仓文档

- `docs/web-api/endpoints/app_{install,session,control}.md` 及 `docs/zh/web-api/endpoints/` 同名三篇、`docs/public/web-api/endpoints-manifest.json`、`docs/web-api/endpoints/manifest.json`：HTTP API 文档，rename 路由时需同步（其中仅 app_session 两篇含 sidecar/appsite 关键词命中，其余为标题级关联）。
- 其余 `Docs/`（v4.3.1 历史）与 AppHost 无关。

---

## 3. App 仓（camthink-ai/ne30x-app @ 460c8cd1）闭合

结构（`git ls-files` 147 文件）：

| 区 | 内容 | 与固件仓的耦合 |
|---|---|---|
| `apps/line-crossing/{src,include}` | 纯业务核心（tracker/line_cross），Makefile 自述 "ported from ne301 counting@de25a6f1"，host 可编译 | 无路径耦合（注释性来源标注） |
| `apps/line-crossing/app/` | v4 entry 合并体：`lc_app_entry_v4.c/h`、`lc_app_abi_v2/v3/v4.h(+layout_asserts)`、lc_bus/stateblob/ctrl/cfgcodec/json/arena/libc_mini/lvn4；`app/Makefile`：`EXEC_BASE ?= 0x93E00000`、`V4_APP_VERSION ?= 4.2.14`、`-T lc_app.ld`；`lc_app.ld`：`APP(rx) ORIGIN=0x93E00000 LENGTH=2M`、`ENTRY(app_entry)`、`ASSERT(SIZEOF(.data)==0)`（Host 不初始化 .data → **冻结布局约束**） | 地址 0x93E00000/2M 与固件 `APP_HOST_RAM` 硬耦合 |
| `apps/line-crossing/ui/` | React UI（38 src 文件），`src/runtime/bridgeProtocol.ts:1` `BRIDGE_PROTOCOL='ne301.sidecar.bridge.v1'`（协议值，冻结） | 协议字符串含 "ne301" 字样 |
| `package/release/` | `build_neapp.sh`（UI build → make elf-v4 → make neapp-v5 → neapp_verify_v5.py --host-caps-hex 0x7ff → verification.json）、`release_tool.py`（版本/notes/provenance 校验）、`known-limitations.md` | 产物 `line-crossing-<PRODUCT_VERSION>.neapp` |
| `package/unsigned/` | `neapp_{pack,verify}_v5.py`、`neapp_v5_{format,image,native,product_version,ui_assets}.py`：`NATIVE_MAGIC=b"NEA1"`、`NATIVE_HEADER_SIZE=32`、`HOST_ABI_V4=0x00040000`、`native_target_addr` 必须落在 Host 执行区 | 与固件 `app_host_abi.h` 的 magic/header 常量集合（NEA1/32B/format 1）一致；ABI 语义见 §AC3（v5 容器内 native=v4） |
| `tests/` | line-crossing（golden 等价）、line-crossing-app/-enable/-live/-runtime/-ui、release、unsigned、fixtures `line-crossing-4.0.0.neapp(+provenance)` | **跨仓 pin（见下）** |
| `.github/workflows/` | `release-app.yml`（main 手动触发，产 Release）、`neapp-unsigned.yml`（PR/push 验证） | 自包含，不引用固件仓 |

### 跨仓硬耦合（双仓闭合核心结论）

App 仓 → 固件仓（路径字面 + SHA pin；`git grep -n "HOST_SHA=\|Custom/Services/AppHost" -- tests/`）：

1. `tests/line-crossing-enable/run_joint_matrix.sh:7` `HOST_SHA=dc0948c1e12e954612fbfa9769434bf01e449c3c`（pin 历史固件提交）；`:73-101` 编译 `$HOST_SNAPSHOT/Custom/Services/AppHost/app_host_control.c` 并链接 `$HOST_SNAPSHOT/tests/app_native_vnext/*.o`；并以 `rg 'app_native_vnext_floor_raise\('` 生产命中数=1 作为门（**符号名被跨仓测试门引用**）。
2. `tests/line-crossing-live/run_tests.sh:11` `HOST_SHA=4c1c0d7ec5bc8ae18fae4ca5a31f0699b7c5895b`；`:37-40` `git archive` 提取 `Custom/Services/AppHost/app_host_live.{c,h}`、`app_admission.h`、`Custom/Common/Inc/aicam_types.h`（**目录名进入 git archive 路径参数**）。

固件仓 → App 仓：`tests/app_host_enable/run_app30_joint.sh:6-7` `APP_REPO`/`APP_SHA=85e3d4a46b8b4c53a63fac3405b91efb084a3f8a`；`tests/sidecar_host_bridge/fixtures/line-crossing.neui.provenance.json` pin `1f48bdd5...`（fixture 内登记，不执行 clone）。

推论：pin 的是**历史 SHA 的 git archive**，因此固件仓**未来** rename 不破坏这些脚本对**旧 SHA** 的重放（对象库仍在）；但任何**更新 pin SHA** 或新增联合门时，路径/符号名必须同步——rename 的双仓协调点即此两脚本 + `run_app30_joint.sh`。

---

## 4. UNKNOWN（诚实登记）

| # | UNKNOWN | 影响 | 建议取证方式 |
|---|---|---|---|
| U1 | 固件 flash footprint 中 AppHost 实际占用的 .text/.data 字节（本次为 R0 静态审计，未做 HaLow 配置全量构建与 map 解析） | AC4 容量矩阵只能给"可离线复现"的测量方法，不能给已证实数字 | `APP_HOST_POC=1` + HaLow def 下 `make app` 后解析 `Appli/build/*.map` 中 AppHost 目标文件聚合 |
| U2 | "约 270 KB 整树增量"的原始口径（#172 PRD v0.22–v0.24 编辑历史）本次未独立复现；不把它当作可删除 Host 字节 | 只作历史线索引用 | A 提供 PRD 版本区间，B 可用 `git diff --stat <base> cfb90ed22` 复算 |
| U3 | ne30x-app `460c8cd1` 与固件 `cfb90ed22` 之间"官方对照"是否即为 Line Crossing v1.0.2 发布对（provenance 中 `product_version 1.0.2` 未见直接文件，README/known-limitations 未逐字核对版本号） | 若发布对含其他固件 SHA，兼容门槛结论范围需 A 确认 | A 确认发布对固件 SHA；或比对 release-app.yml 产物 provenance |
| U4 | `tests/app_installer/fixtures/line-crossing-4.2.12.neapp` 与 `line-crossing-1.0.1.neapp` 的生成来源（make_product_version_fixtures.py 可再生成，但 1.0.1 是否绑定外部发布物未证） | fixture 改名/重生成边界 | 运行 `python3 tests/app_installer/make_product_version_fixtures.py` 比对哈希 |
| U5 | 非固件消费者（第三方工具/脚本）是否硬编码 `/api/v1/app/*` 路由或 `Custom/Services/AppHost` 路径（仓外不可穷举） | rename 外部接口的风险余量 | A 决定是否需要对外公告窗口 |
| U6 | `docs/web-api/endpoints/manifest.json` 与 C 路由表是否自动同步（`Script/gen_web_api_docs.py` 未引用 app 模块，疑手工维护） | 文档漂移风险 | 读 manifest.json 生成说明 |
