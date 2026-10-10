# 工厂/维修首启授权与 NVS 直写旁路只读取证（Issue #45）

- Issue: harryhua-ai/ne301#45（以 issue 内 `ght-contract` 为准）
- 调查基线: 分支 `agent/45/2c03b4bb`（HEAD = experiment 基线 `20e89fbc`）
- 被审固定参考 Candidate: `agent/37/ef9ccc5f` = `671ae4c5`（已正式交付、被 A `REQUEST_CHANGES` 的存储安全 Candidate，PR #38）
- 定位: **只读调查证据文档，不是规范，不是产品决策权威**。本会话零真机操作、零固件构建、零设备/工装接触。AC3 只提交选项与阻塞清单供 A 决策，不判定生产授权方案。
- 证据等级标注:
  - 【源码】= 给出 `file:line` 锚点的静态源码事实。锚点默认指向 `agent/37/ef9ccc5f`（下称 C@ 行号）；与当前工作树 `20e89fbc` 逐字节相同的文件（已用 `git diff` 验证：`cli_cmd.c`、`factory_test.c`、`debug.c`、`device_service.c`、`system_service.c`、`web_server.c`、`wake_scheduler.c`、`drtc.c`、`upload_coordinator.c`、`wifi.c`、`sl_net_netif.c`、`nvs.c`）直接引工作树路径。C 分支改动的文件（`storage.c/h`、`storage_safety.*`、`json_config_*`、`auth_mgr.c`、`api_device_module.c`）以 C 分支行号为准。
  - 【推断】= 静态语义推导的调用链/后果，**未在真机复现**。
  - 【未知】= 仓库内/本会话不可取得的外部事实（工装、生产流程、出货配置），**需外部验证**。
- 背景衔接: `Docs/design/storage-safety-baseline.md`（C 分支内，#37 候选文档）§2 三态不变量、§6.5"AC3 首启授权入口 blocked on A/User"；`Docs/design/app-install-feasibility.md`（#27）§1.2/§1.3 存储事实、§1.3a SWD 工具历史。

---

## 0. 结论摘要

1. **AC1：不存在任何可证实授权的 NVS 写入口。** 全部直写 NVS_USER/NVS_FACTORY 的入口（UART CLI、factory 命令族、固件内部自动写）没有任何设备身份核验、操作者认证、一次性凭证或重放防护；唯一有认证的 Web 面依赖公开默认凭据 `admin/hicamthink`（且该凭据在存储健康时即有效），**不构成独立信任锚**。仓库内无任何可信工厂工装/凭证证据 → 按契约标 **BLOCKED**。
2. **AC2：底层 `storage_nvs_write/delete/clear` 只查 `is_init` + `ready`，不查 `persist_blocked`/PENDING_INIT/Web 认证。** persist_blocked 门只存在于 json_config 配置层。CLI `fset` 可向 NVS_USER 写任意 key（含 `cfg_magic`/`auth_password`），静态链路上两条命令即可在空白/健康任意状态下注入管理员凭据（【源码】+【推断】，未真机验证）。`mem w` 提供任意内存写原语，可绕过包括 ready 门在内的一切软件门。三个固件内部写者（时区/唤醒状态/上传满标记）与 WiFi 升级流同样无 persist_blocked 检查——**PENDING_INIT 会话的"空白保持"不被底层保证**。
3. **AC3：受控首启的授权语义（谁有权把空白变有效）是缺失的产品/固件边界，不是已有能力的开关问题。** 提交 4 个可独立验收的最小修改范围选项（底层收口门 / 受权 CLI 模式 / 生产镜像裁剪 / 独立信任锚前提），均不含生产方案判定。

---

## 1. AC1：离线工厂/维修入口与授权保护清单

### 1.0 介质与访问面事实

- NVS 分区 64KB = NVS_FACTORY 32KB + NVS_USER 32KB（`Custom/Hal/storage.h:37-42`，C 分支）。所有配置/凭据 key 存 NVS_USER；工厂数据（serial/hw_version/mfg_date/mac_address/test_passed/wifi_*）存 NVS_FACTORY。
- 调试控制台任务在 core init 基础阶段无条件启动（`Custom/Core/core_init.c:304`），UART 为 huart1/huart2（按板型宏，`Custom/Core/Log/debug.c:46-49`），**命令路径全程无认证**（IRQ→队列→cmdline→命令表分发，`debug.c:556-604`，无任何口令/会话检查）。
- Web 服务监听 `0.0.0.0`（AP+STA 全接口，`web_server.c:82-92`）。
- FSBL 不访问 NVS（`git grep` FSBL/ 无 `storage_nvs`/`nvs_write` 命中）。【源码】

### 1.1 入口清单（穷举 `storage_nvs_write|storage_nvs_delete|storage_nvs_clear|NVS_USER|NVS_FACTORY` 全部调用方后归并）

| # | 入口 | 触发前提 | 身份核验/授权机制 | 与公开默认凭据独立性 | 可写分区/数据 | 等级 |
|---|------|----------|-------------------|----------------------|---------------|------|
| 1 | UART CLI `fset <key> [value]` / `fset <key>`（写/删） | 串口物理接触 | **无**（无认证、无 persist_blocked、无白名单） | 不适用（该面无凭据可言） | NVS_USER 任意 key | 【源码】`cli_cmd.c:358-373`，注册 `cli_cmd.c:1222` |
| 2 | UART CLI `fget [key]` / 无参 dump（读） | 串口物理接触 | **无** | 不适用 | 读 NVS_FACTORY+NVS_USER **全部 key 并明文打印**（含 `auth_password`/`dev_info_password`） | 【源码】`cli_cmd.c:324-352`；`storage.c:1016`（C@） |
| 3 | UART CLI `factory sn <sn>` | 串口物理接触 | **无** | 不适用 | NVS_FACTORY `serial_number` + NVS_USER `dev_info_serial` | 【源码】`factory_test.c:723-737` |
| 4 | UART CLI `factory hw <ver>` | 串口物理接触 | **无** | 不适用 | NVS_FACTORY `hw_version` + NVS_USER `dev_info_hw_ver` | 【源码】`factory_test.c:739-763` |
| 5 | UART CLI `factory mac <mac>` / `mac clear` | 串口物理接触 | **无**（仅格式合法性校验 `factory_mac_is_valid`） | 不适用 | NVS_FACTORY `mac_address` 写/删 + NVS_USER `dev_info_mac` 删/镜像重置 | 【源码】`factory_test.c:766-829` |
| 6 | UART CLI `factory mark` | 串口物理接触 | **无** | 不适用 | NVS_FACTORY `mfg_date`/`factory_fw_ver`/`hw_version`/`test_passed`（产测通过标记可伪造） | 【源码】`factory_test.c:831-855`、`610-648` |
| 7 | 导出 API `factory_config_write()`（整包工厂配置写） | **无调用方**（死代码/导出 API） | **无** | 不适用 | NVS_FACTORY+NVS_USER 全部身份数据 | 【源码】`factory_test.c:491-537`（调用方 grep 仅定义+头文件声明） |
| 8 | UART CLI `factory test`（NVS 项） | 串口物理接触 | **无** | 不适用 | NVS_USER `_factory_test_` 测试键写/删 | 【源码】`factory_test.c:110-142` |
| 9 | UART CLI `factory reset` | 串口物理接触 | 走 `device_service_reset_to_factory_defaults` → **persist_blocked 门**（见 §2.2 R1）；健康会话即可执行，**非授权面** | 依赖会话状态，与凭据无关 | NVS_USER（复位为默认值） | 【源码】`factory_test.c:856-879`、`device_service.c:2505+`、`json_config_mgr.c:706`（C@） |
| 10 | UART CLI `mem w <addr> <val>`（任意内存写） | 串口物理接触 | **无**（可改写 RAM 中任意标志/函数指针，含存储门本身） | 不适用 | 一切（含间接 NVS） | 【源码】`cli_cmd.c:293-322`（w 分支 311-315） |
| 11 | UART CLI `format`（LittleFS） | 串口物理接触 | **无**（Web 同功能需认证+`confirm:"FORMAT"`，CLI 版无） | 不适用 | LittleFS 全卷（非 NVS，相邻破坏面） | 【源码】`cli_cmd.c:272-278`；对照 Web 版 `api_device_module.c:2206-2246,2321-2325`（C@） |
| 12 | UART `ymodem receive` | 串口物理接触 | **未实现**（模式切换桩，接收逻辑 TODO） | 不适用 | 无（当前无写路径） | 【源码】`debug.c:259-278`（命令）、`debug.c:587-592`（IRQ TODO）；库 `Custom/Common/Utils/generic_ymodem.c` 无固件侧调用方 |
| 13 | 物理按键 10s 超长按 → 出厂复位 | 物理接触按键 | 无认证；走受门链（§2.2 R1）——PENDING_INIT/降级会话拒绝，健康会话执行 | 与凭据无关 | NVS_USER 复位为默认 | 【源码】`device_service.c:910-917`、`system_service.c:600-606` |
| 14 | Web `POST /api/v1/system/factory-reset`（延时任务） | 网络可达 + Basic 认证（`require_auth=TRUE`） | Web Basic auth = `admin` + NVS 口令（或默认口令）；`credentials_untrusted` 时全拒 | **不独立**：口令源头是 NVS 或公开默认 `hicamthink` | NVS_USER 复位（受门链） | 【源码】`api_device_module.c:1945-2045,2419-2423`（C@）；`web_server.c:689-694,1048-1079`；`auth_mgr.c:158-162`（C@）；默认值 `json_config_mgr.c:72`、`auth_mgr.h:60`（C@） |
| 15 | Web `POST /api/v1/device/storage/format` | 网络可达 + 认证 + `{"confirm":"FORMAT"}` | 同上（Web auth） | 不独立 | LittleFS（非 NVS） | 【源码】`api_device_module.c:2206-2246,2321-2325`（C@） |
| 16 | Web 配置类 API（网络/AI/图像/MQTT…全部经 gated helper 落 NVS_USER） | 网络可达 + 认证 | Web auth + persist_blocked 门（§2.1） | 不独立 | NVS_USER 配置 key | 【源码】`json_config_nvs.c:25,3172-3282`（C@） |
| 17 | Web WiFi 固件 OTA → `wifi_mark_update_pending` | 网络可达 + 认证（OTA 会话 armed 门） | Web auth + OTA 三重门（#27 §2.0） | 不独立 | NVS_FACTORY `wifi_fw_source`/`wifi_mode` | 【源码】`api_ota_module.c:1222,1709` → `wifi.c:760-767` |
| 18 | 固件内部自动：WiFi 模块固件缺失自恢复 → `wifi_enter_update_mode`（写 NVS_FACTORY + 重启） | 网络初始化时检测到模块固件无效且 `update_times<3`；**无人触发** | **无任何门** | 不适用 | NVS_FACTORY `wifi_fw_source`/`wifi_mode`（`wifi_update_times` 亦由固件写） | 【源码】`sl_net_netif.c:3481-3483` → `wifi.c:745-767`；`wifi.c:680,729,750-751,763-764,850` |
| 19 | 固件内部自动：时区变更写 `TIMEZONE_NVS_KEY`（含 SNTP/网页自动同步时间源） | 运行时时区变化 | **无 persist_blocked 检查** | 不适用 | NVS_USER `timezone` | 【源码】`drtc.c:692-701` |
| 20 | 固件内部自动：唤醒调度状态持久化 | 运行时标记 | **无 persist_blocked 检查** | 不适用 | NVS_USER wake state | 【源码】`wake_scheduler.c:52-63` |
| 21 | 固件内部自动：上传"存储满"标志持久化 | 运行时状态翻转 | **无 persist_blocked 检查** | 不适用 | NVS_USER `upload_storage_full` | 【源码】`upload_coordinator.c:206-210` |

### 1.2 排除项（系统排查后确认**无** NVS 写路径的面）

| 面 | 排查结论 | 证据 |
|----|----------|------|
| SD 卡 | 文件面（上传/读取，Web 认证）；**无"从 SD 导入配置→写 NVS"路径**：`json_config_load_from_file` 唯一调用方是 CLI `config_show`（只读展示）；SD 上的 WiFi 固件文件由固件读取（读，非配置导入） | 【源码】`cli_cmd.c:419`；`api_file_module.c`（上传需认证）；`wifi.c:738-751`（FILE 源为读路径） |
| USB 设备口 | 仅 UVC 相机类（无 MSC/CDC 数据面），无 NVS 访问 | 【源码】`Custom/Hal/usbx/usbx_conf/usbx_conf.c`（`ISP_ENABLE_UVC` 分支，全程无 mass storage 类） |
| BLE | WiFi 模块（RS911x）的 RF 测试命令（经 CLI 注册 `ble`），无配对流程、无 NVS 写 | 【源码】`netif_manager.c:1046`；`Custom/Hal/Network/sl_rsi_ble/sl_rsi_ble.c:1140-1158` |
| FSBL | 无 NVS 访问 | 【源码】git grep 无命中（§1.0） |
| SWD/编程器（物理调试口） | 硬件级直写整片 Flash 的能力存在（#27 §1.3a 记录实验会话曾用 SWD 整卷部署）；**不经固件任何门**。它是否构成"受控授权入口"取决于仓库外工装事实 | 【源码/历史】#27 §1.3a；工装事实【未知】 |

### 1.3 AC1 判定

- 全表 21 个入口中，**没有一个**具备：设备身份核验（序列号绑定/挑战应答）、一次性凭证、重放防护、操作者受信机制。有认证的只有 Web 面（#14-17），而其凭据源头是 NVS 自身或公开默认值 `admin/hicamthink`——**自我引用 + 公开默认，不构成独立授权来源**。【源码】
- 串口可达 = NVS 全写权（#1-#12）。串口接触本身不是授权；仓库内**没有任何受信工装、工厂凭证、签署流程的代码或文档证据**（`git grep 工装/产测/factory tool/provisioning` 在 Docs/Script/Custom 无产品级命中，仅第三方库误中）。【源码】+【未知】
- **AC1 结论：BLOCKED**——当前仓库无法证明存在"可受信的离线首启授权来源"。与 #37 候选文档 §6.5 的声明一致（现有已批准产品流程中不存在可执行、可审验的独立授权首启操作）。

---

## 2. AC2：NVS 直写旁路调用路径扫描（固定 Candidate，静态分析，未真机验证）

### 2.1 门的事实（自底向上）

| 层 | 检查 | 缺失的检查 | 锚点（C@） |
|----|------|-----------|------------|
| NVS 库 | `nvs_write/nvs_read/nvs_delete/nvs_clear` 均查 `fs->ready` | 无业务门（设计如此，库层只管介质状态） | `nvs.c:697/858/796/639`（ready 检查 706/874/810/644）；`nvs_init` 成功置 `ready=true`（`nvs.c:659,684`）——**空白分区 init 成功即 ready，无需擦除** |
| storage 门面 | `storage_nvs_write`：`is_init`（943）+ `ready`（955）；`storage_nvs_delete`/`clear`：仅 `is_init`（983/1001，ready 由库层兜底） | **不查 `json_config_mgr_persist_blocked()`、不查 PENDING_INIT、不查 Web 认证** | `storage.c:940-996`（C@）；`storage_nvs_init` 失败→分区保留、not-ready（`storage.c:905-935`，C@） |
| 配置层 | per-key 写 helper 与全量保存 choke 查 `persist_blocked`；ISP 保存入口整体门控；凭据 key 保存受 `credentials_trusted` 约束 | 只覆盖**经 json_config 的路径**；直接调 `storage_nvs_*` 的调用方全部不受控 | `json_config_nvs.c:25`（inline helper）、`3172-3282`（per-key 门）、`872-877`（ISP 门）、`902,953,963,986`（门内 raw blob 写）（C@）；`json_config_mgr.c:298-301`（C@） |
| 引导分类 | 仅 PERSISTED 允许持久化/管理认证/出厂复位 | 分类结果是**会话 RAM 标志**，对不经过它的调用方无约束力 | `json_config_boot_policy.c:11/44/54/65`（C@）；`auth_mgr.c:158-162` 凭据不可信全拒（C@） |
| Web 层 | 路由 `require_auth` + Basic 认证 | 只覆盖 HTTP 路由 | `web_server.c:689-694` |

**关键缺口：`persist_blocked` 是配置层会话标志，不是介质写保护。** 任何绕过 json_config helper 直接调用 `storage_nvs_write/delete` 的代码路径，以及任何非 HTTP 的外部输入面（UART/按键），都不受其约束。【源码】

### 2.2 旁路矩阵（按旁路组；后果按四情景）

四情景定义：S1 出厂空白（NVS_USER 全 blank → PENDING_INIT）；S2 异常读取（magic 非法/单 key UNKNOWN/后端 not-ready）；S3 部分写入+重启（掉电撕裂后状态）；S4 健康旧配置（PERSISTED）。

| 组 | 路径与可达前提 | S1 出厂空白 | S2 异常读取 | S3 部分写入+重启 | S4 健康旧配置 |
|----|----------------|-------------|-------------|------------------|----------------|
| **B1** `fset` 任意写 NVS_USER | 串口即可；底层只查 ready，**S1 下 blank init 成功即 ready → 可写** | **完全绕过 PENDING_INIT**。`fset cfg_magic 1095320385`（=0x41494341，uint32 以十进制字符串解析，`json_config_nvs.c:44-51,1847` C@）+ `fset auth_password <x>`（明文存储/加载，`json_config_nvs.c:2020-2062` C@；`auth_mgr.h:60` 默认同理）→ 下次启动分类 PERSISTED + `CRED_PROVEN` → 注入口令成为管理员口令【推断，静态链路，未真机复现】 | magic 非法（UNRECOGNIZED）时可被 `fset cfg_magic` 翻为 PERSISTED；后端 not-ready（BACKEND_UNAVAILABLE）时底层写失败 `-1`——此情景下唯一被介质状态挡住的子情景【源码+推断】 | fset 直写无原子性，可加剧或任意改写撕裂状态；`fget` dump 可侦察现状 | **`fset auth_password <x>` 一条命令 → 下次启动静默管理员接管**（读回即 proven → trusted）；`fget` 可直接读出当前口令【推断】 |
| **B2** `factory sn/hw/mac/mark` | 串口即可 | 可在无授权情况下预写身份数据（serial/MAC/产测标记）——**身份数据写入≠授权**，但使"空白可独立证明"的判定复杂化（写入后不再全 blank） | 可篡改身份/MAC（影响 SSID/MQTT 身份派生，`factory_test.c:766-829`） | 可写入半套身份数据（与固件启动期 re-stamp 交互，`factory_test.c:515-530` 注释） | 身份数据/产测标记可被伪造或清改 |
| **B3** `mem w` 任意内存写 | 串口即可 | 可置 `nvs_fs_t.ready=true`（击穿 not-ready 门）、清 `persist_blocked`/`credentials_untrusted` 标志、改写函数指针；PSRAM 默认 RWX（#27 §1.5）→ 理论可注入代码【推断，静态；MPU 仅 2 区域，`Appli/Core/Src/main.c:171-214`】 | 同左——**唯一能绕过 S2 介质门的原语** | 同左 | 同左 |
| **B4** 固件内部旁路写（#19/#20/#21 + WiFi 升级流 #17/#18） | 运行时自动触发，无需任何操作者 | **结构上无门**：后端 ready 即写。即 PENDING_INIT 会话中时区/唤醒状态/上传标记/WiFi 模式键仍可落盘——**"proven-blank 保持空白"不被现有代码保证**【源码（缺门是事实）+推断（S1 下实际触发时序未真机验证）】 | 同左（后端 ready 时）；后端 not-ready 时随介质失败 | 同左 | 正常功能路径 |
| **B5** `storage_nvs_clear`（整分区擦除） | **树内无调用方**（死代码）；若未来接线则仅受 ready 约束 | — | — | — | — 【源码】grep 全树仅定义 `storage.c:998`（C@） |
| **B6** CLI `format`（LittleFS，非 NVS） | 串口即可，无认证 | 邻近破坏面：S1-S4 任意状态下可整卷格式化文件区 | 同左 | 同左 | 同左【源码】 |
| **B7** 已门控路径（对照，无旁路）：Web 出厂复位/格式化、json_config helper 写、ISP 保存、凭据保存 | 认证 + persist_blocked + confirm | 拒绝（#37 候选 host 注入测试 §4 #12/#18） | 拒绝（fail-closed） | 拒绝 | 健康 PERSISTED 会话按设计执行 | 【源码】`json_config_mgr.c:706`、`json_config_nvs.c:25/872`（C@）；auth 拒绝 `auth_mgr.c:158-162`（C@） |
| **B8** ymodem/USB/BLE/SD 配置导入（对照，无路径） | — | 未实现/不存在（§1.2） | — | — | — 【源码】 |

### 2.3 补充事实（与授权面相关）

- `fget` 无参 dump 会明文打印 NVS 全部 key 值（`cli_cmd.c:331-338` + `storage.c:1016-1044`，C@）——包含管理员口令明文。UART 既是写旁路也是**凭据读出口**。【源码】
- Web 认证路径将 Authorization 头与口令明文写入日志（`web_server.c:1059,1072`）——串口日志因此成为第二凭据泄露面（侧发现，归 #36 处理）。【源码】
- NVS_FACTORY 的 `serial_number`/`mac_address` 等无任何防篡改校验（无签名/CRC 之外的完整性机制），`fget`/`factory config` 直接信任读回值。【源码+推断】

### 2.4 AC2 判定

- **不能以"json_config_mgr 已 fail-closed"当作全局 PASS。** 至少 B1（fset）、B2（factory 族）、B3（mem w）、B4（四个内部写者+WiFi 升级流）是 persist_blocked/PENDING_INIT 之外的等价或更宽入口；其中 B1/B3 在静态链路上可完全击穿 #37 候选建立的三态下限（S1 的拒绝式安全、S4 的凭据可信性）。
- 以上全部为**静态分析结论，未做任何真机验证**；不将可能性冒充已证实漏洞。B1 的端到端接管链（fset→重启→Web 登录）是真机遗留验证项（§5）。

---

## 3. AC3：最小可行选项与阻塞清单（供 A 决策，不做产品判定）

### 3.0 前提判定

若以"存在可信离线首启授权来源"为标准：**当前不存在**（AC1 BLOCKED）。因此任何"受控初始化"实现都需要同时回答两层问题：(a) 旁路面收口（固件内，可工程化）；(b) 授权语义/信任锚（产品决策 + 可能的仓库外工装，**本任务无权设计**）。

### 3.1 选项清单（每个可独立验收；非互斥，建议按 O1→O3 递进）

| 选项 | 内容 | 改动面 | 验收方式 | 残余风险 |
|------|------|--------|----------|----------|
| **O1 底层收口门** | 在 `storage_nvs_write/delete/clear` 门面（storage.c）加会话门：非健康 PERSISTED 会话拒绝写/删/擦（或按 A 定义的 key 白名单放行最小必要项，如 WiFi 恢复键）。单一 choke point 覆盖 B1/B2/B4 全部旁路组 | `Custom/Hal/storage.c`（+一个查询接口的接线，约数十行）；配套 host 注入测试 | host 注入：PENDING_INIT/UNRECOGNIZED 会话下调用 fset/factory/时区路径→断言拒绝且介质逐字节不变；健康会话回归不变；目标编译 0 warning | `mem w`（B3）仍可击穿（内存原语不在介质层可防范围）；放行白名单本身是 A 的产品决策；不解决"谁授权首启" |
| **O2 受权 CLI 模式** | 串口保留只读诊断；`fset/factory <write>/format/mem` 类写命令要求显式受权会话（如：设备屏幕/LED 给出一次性码 + 工装应答，或物理按键组合 + 时限窗口）。**授权原语的设计超出本任务，此处只列边界** | `debug.c`/`cli_cmd.c`/`factory_test.c` 命令分层面 + 新授权校验模块 | host：无授权写命令拒绝、授权窗口内放行且超时失效；目标：真机工装流程演示（需真机，当前 BLOCKED） | 依赖一个独立信任锚（一次性码如何防串口侧即时窃取？）——若信任锚仍是设备自身输出，安全强度有限；授权原语未定义前 **BLOCKED on A/User** |
| **O3 生产镜像裁剪** | 生产构建条件编译移除/降级 CLI 写命令（fset/factory 写、mem、format），或出厂后在首次受控初始化时熔断调试写面；保留只读诊断 | `cli_cmd.c`/`debug.c`/`factory_test.c` 条件编译或运行时熔断标志 | 构建断言：生产镜像符号表中无写命令；host+目标编译；熔断后写命令返回拒绝 | 维修场景需重新开启通道（流程设计归 A）；熔断标志若存 NVS 则可被 fset 伪造（须先有 O1/O2）；SWD 物理口不在此控制范围 |
| **O4 独立信任锚/工装前提（非固件实现项）** | 若要求可证明的工厂授权（设备身份核验、一次性凭证、防重放），需要引入仓库当前不存在的机制：设备级唯一密钥（OTP/secure element）、签名的工装凭证、或受控 SWD 工装流程与审计记录。**仅列为前提，不设计方案** | 无（前提清单） | 由 A/User 定义威胁模型与验收后另立任务 | 供应链/密钥托管成本；#27 §1.3a 显示 SWD 路径在实验环境已可整卷直写——物理口在位本身即是边界条件 |

### 3.2 与既有任务的分工边界建议

- **#37（存储安全基线）**：三态不变量、PENDING_INIT 拒绝式下限、persist_blocked/credentials_trusted 门与 host 回归——维持 #37 范围。本调查证明该门**不是**全局写保护，#37 恢复时的修订范围可参考 O1 是否纳入。#37 §6.5 的"AC3 首启授权入口 blocked on A/User"与本 AC1 BLOCKED 结论互为印证。
- **#36（管理员认证）**：默认口令生命周期、口令明文日志（§2.3）、Web 会话强度、CLI 日志泄露面——归 #36。本调查仅记录事实。
- **既有 CLI 调试能力**：fset/factory/mem/format 是开发调试面；生产化处理（O2/O3）属新边界，不应混入 #37 的存储保护语义。

### 3.3 阻塞清单（无法在本任务内消除）

1. **可信工装/工厂流程事实**【未知】：是否存在正式产测工装、如何写 SN/MAC、凭证如何管理与审计——仓库内零证据，需 User/制造方提供。
2. **授权语义**：首启授权的信任锚（设备身份 vs 操作者凭证 vs 物理在场证明）未定义——A/User 决策。
3. **生产镜像的 CLI 配置**【未知】：当前源码默认编译全部命令；实际出货镜像是否裁剪，仓库不可证。
4. O2/O4 的实现前提（真机演示、信任锚选型）超出本只读任务授权。

---

## 4. 未知项汇总（均不作为已证实事实引用）

| # | 未知项 | 验证需求 |
|---|--------|----------|
| U1 | 仓库外实际工厂/维修工装与凭证体系（存在性、能力、审计） | User/制造方提供工装文档与流程证据 |
| U2 | 出货固件是否包含本调查所列 CLI 写命令（当前源码默认包含） | 出货镜像符号表/配置核对 |
| U3 | B1 fset→重启→接管链的真机端到端行为（含 uint32 十进制解析、`cfg_magic` 翻转分类的实际分类结果） | 受控真机注入实验（需 A 授权） |
| U4 | `mem w` 置 `ready=true`/清会话标志的实际效果（编译优化后标志布局、缓存一致性） | 受控真机实验 |
| U5 | S1（PENDING_INIT）会话下 B4 内部写者的实际触发时序（SNTP 默认是否开启、浏览器时间同步是否经认证 API）——缺门是源码事实，触发与否依赖运行时 | 启动日志 + 网络环境受控复测 |
| U6 | 出厂时 NVS_FACTORY 初始内容（工装是否预写 wifi_*/serial；WiFi 自恢复流 #18 在产线是否会首启触发） | 出厂分区镜像抽样 |
| U7 | NVS 库在 S3 掉电撕裂下的实际恢复行为（ATE/CRC 回放覆盖度） | 真机掉电注入（#37 §6.1 同族开放项） |
| U8 | Web 前端/APP 的设备发现与配网流程是否引入本调查未覆盖的写路径 | 移动端/前端仓库核对 |

---

## 5. 范围与边界重申

- 本任务唯一产出：本文档（`Docs/design/**`，ght-contract scope 允许项）。零代码/Makefile/测试改动；零 ght 写命令、push、PR；零设备/Flash/工装操作。
- 全部结论基于 `agent/37/ef9ccc5f`（671ae4c5）与 `20e89fbc` 的静态源码；引用行号已在 §0 说明分支归属。
- 本文档不判定生产授权方案、不宣告任何 PASS、不代表 #37 attempt 状态变更（#37 的 release/blocked 处置仍按其自身流程独立进行）。
