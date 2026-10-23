# NE301 已安装可信 App 管理员手动单次执行入口与状态（Issue #31 实施设计）

本记录中的 App 签名与发行者信任链已由 #136 退役。当前 Host 格式和安装策略见 [NEAPP05 无签名单文件格式](./app-unsigned-single-file-format.md)。

- Issue: harryhua-ai/ne301#31（`ght-contract` AC1–AC3）
- 基线: `experiment/app-host-poc@8c8c907a607bcc4fe9a4a99b2d0d8a85f2972739`（= #30 合并 HEAD，含 app_installer 全套）
- 协议权威: [ne30x-app `docs/app-package-protocol-v1.md`](https://github.com/harryhua-ai/ne30x-app/blob/main/docs/app-package-protocol-v1.md)（§3.1/§3.2/§4.1/§7.1）；本文与其冲突时以该规范为准
- 本文档性质: B 实施记录（HOW），不是协议权威，不是设备/生产安全认证

## 0. 结论速览

| 交付物 | 位置 | 状态 |
| --- | --- | --- |
| 手动单次执行编排核心（当前重验 + 互斥 + 原始返回码，纯逻辑 host 可测） | `Custom/Services/AppHost/app_host_run.{c,h}` | 完成，host 174/0 |
| 目标执行后端（执行区暂存/换存 Cache 维护/入口调用/UART 日志） | `Custom/Services/AppHost/app_host_run_device.c` | 完成（仅 `APP_HOST_POC=1` 构建） |
| 管理员运行 API `POST /api/v1/app/run` + `GET /api/v1/app/installed` 增补 `app_running` | `Custom/Services/Web/api/api_app_install_module.{c,h}` | 完成，gate 154/0 |
| 实验加载器运行互斥守卫（UART `apphost load` 在有 App 运行时拒绝装载） | `Custom/Services/AppHost/app_host.c` | 完成 |
| 运行专用签包向量（锚钥签 ABI 失配包 `ea23ec94…`，加性再生） | `tests/app_installer/sign_packages.py` + `installer_vectors.h` | 完成，锚指纹 `06e732b1…` 断言保持 |
| 宿主测试套件 | `tests/app_host_run/`（新）+ `tests/app_installer/`（gate 增补） | 174/0 + 271/0 + 280/0 + 154/0 + 54/0 |
| docs 生成物（四类窄路径） | `docs/**`（生成器只运行未修改） | `--check` 提交后 OK |

**明确不做**（non-goals 遵守）：不做后台运行/多 App 并发调度/开机自启/强制终止；不宣称沙箱、崩溃隔离或抢占安全停止；不改包格式、信任根、#30 存储事务、冻结 v1 ABI、串口产品策略、OTA；不把 UART `apphost load <path>` 暴露为产品 Web 入口；不实现 #43 持续运行/AI 事件、#47 v2 能力准入、#39 Web UI。

## 1. 执行编排（五条执行不变量的落点）

`app_host_run_installed(io, out)` 每次显式手动运行请求执行以下完整序列（`app_host_run.c`）：

```
POST /api/v1/app/run (仅 APP_HOST_POC=1 注册)
  │ require_auth + web_server_high_privilege_allowed()（先于任何 body 解析；本端点不解析 body）
  ▼
1  后端就绪检查：未 init/缺 stage/commit/invoke 钩子 → RUNTIME_UNAVAILABLE（fail-closed，不冒充可运行）
2  预约互斥：io->tx_begin 失败 → BUSY；app_install_app_running()==TRUE → BUSY（不触碰他人运行标志）
   置位运行标志（与 #30 install/uninstall 同一把事务互斥锁内完成判定+置位）并清空本次日志捕获
3  当前已提交身份：app_install_query_committed()（含惰性 boot 意图解析）
   记录不可读/CRC 坏/状态未知 → STORAGE_STATE_UNKNOWN，拒绝执行；无记录 → NOT_INSTALLED
4  当前信任锚/策略构建（编译期锚 status==provisioned、当前执行区、当前 ABI/caps/板型）
   锚缺失 → UNAVAILABLE/TRUST_UNPROVEN
5  实际字节装载：io->file_stat + io->file_read 把完整 .neapp 容器读入执行区暂存
   stat IO 错误/读 IO 错误 → STORAGE_STATE_UNKNOWN；文件缺失 → UNAVAILABLE/file_missing；
   长度不符 → UNAVAILABLE/content_mismatch；暂存区放不下 → UNAVAILABLE/RESOURCE_LIMIT
6  完整性对已提交身份：SHA-256(实际读入字节) == record.package_sha256，否则 content_mismatch
7  当前信任重验：app_admission_verify_package(实际字节, 当前锚/策略) —
   签名/发行者指纹/板型/PSRAM/ABI/caps/资源/入口限制全部按当前 Host 重新裁决，拒绝类透传为 UNAVAILABLE/<原因>
8  身份一致性：verdict 与已提交记录六元组（publisher/app/版本三元组/tbs/key 指纹）逐一比较，否则 identity_mismatch
9  当前装载约束复验：app_host_validate(容器内原生镜像, 当前执行区约束) — magic/header/format/ABI/target/size/entry/CRC
10 暂存内剥离容器（memmove 原生镜像到执行区基址 + 尾部清零）→ 后端 stage_commit（DSB/Cache clean+invalidate/ISB）
11 以冻结 v1 ABI 调用入口（api 表 = log 捕获+UART 转发 / tick 后端），入口返回期间运行标志保持 TRUE
12 入口实际返回 → 清运行标志 → scrub → 报告 APP_RUN_OK + 原始 int 返回码（不压缩、不映射）
    后端拒绝调用入口 → RUNTIME_UNAVAILABLE（运行状态不可证明安全）
任何拒绝路径：scrub + 清运行标志 + 可解释主原因（reason 字段）
```

- **禁只信历史标志**：步骤 5–9 只消费"本次从存储读出的实际字节 + 当前锚/当前策略"，`installed` 查询状态、文件路径、历史 verified 均不作为执行依据；孤立文件/裸原生镜像无从进入该入口（无 path 参数、无上传）。
- **不可判定 fail-closed**：记录不可读与共享卷 IO 不可判定一律 `STORAGE_STATE_UNKNOWN`（不解释为 not_installed/idle、不清理）；可读但当前信任/ABI/资源不再接受呈现 `UNAVAILABLE` + 主原因（与 #30 `unavailable` 查询语义对齐）。
- **互斥**：运行标志在事务锁内置位，#30 `app_install`/`app_install_uninstall` 在同一锁内检查 → 运行中升级/卸载一律 `BUSY`，执行中字节不可能被替换；同 App 重复运行 `BUSY`；UART 实验加载器新增同标志守卫（-40 拒绝），产品路径与调试路径互不踩踏。无调度器、无会话系统。
- **结果真实性**：`entry_result` 为入口原始 int 返回值（正/负/零均如实透传，不再压成 1）；`entry_returned` 区分"入口已实际返回"与"装载失败/拒绝"；拒绝响应带 `reason`（主分类）+`detail`（如 `content_mismatch`/`PUBLISHER_UNTRUSTED`）；运行中标志仅在入口真正执行期间为 TRUE，重启后 RAM 标志为 FALSE，零自启（注册路径仅 boot 意图解析，从不运行 App），不存在虚假恢复。
- **日志**：入口经 api 表 `log` 输出的文本进入本次运行 512B 捕获环（超限置 `log_truncated`），响应携带 `log`/`log_truncated`；同时经后端转发 UART（`LOG_SIMPLE`），不新增日志通道。

## 2. API 与状态

- `POST /api/v1/app/run`：高权限门 → 执行编排。完成响应 `{"status":"completed", 身份/版本/摘要字段, "entry_returned":true, "entry_result":<原始int>, "app_running":false, "log":"…", "log_truncated":b, "executable":false}`；拒绝响应 `{"status":"refused","reason":"<NOT_INSTALLED|UNAVAILABLE|STORAGE_STATE_UNKNOWN|BUSY|LOAD_FAILED|RUNTIME_UNAVAILABLE>","detail":"<可解释原因>","executable":false,"app_running":b}`。
- `GET /api/v1/app/installed`：全部响应变体增补 `"app_running"`（当前是否有入口在执行，RAM 真值，重启即 FALSE）。加性字段，不改变既有语义。
- 错误词汇与协议 §7 主分类对齐：`BUSY`（已知运行中/事务占用）、`RUNTIME_UNAVAILABLE`（后端无法证明执行状态）、`STORAGE_STATE_UNKNOWN`（不可判定，独立于存储满/提交失败）。HTTP 状态码终映射仍待 P5。
- 路由数 3→4；`Script/gen_web_api_docs.py` 只运行未修改，四类窄路径生成物已再生（`--check` 在提交后 OK）。

## 3. 目标后端与资源边界

`app_host_run_device.c` 仅做平台事：暂存基址=执行区（`__app_host_ram_*`）、Cache/屏障维护、Thumb 入口调用、UART 日志、`osKernelGetTickCount`。共享逻辑不做任何 HAL 依赖，host 全量可测。

**诚实边界**：整包容器先入执行区暂存，故 `package_len > 当前执行区` 时拒绝为 `UNAVAILABLE/RESOURCE_LIMIT`（协议上限 2,097,400B 略大于 2MiB 执行区，超限包可安装但不可经本入口运行，fail-closed）。原生镜像 `target_addr` 以刚验签 verdict 钉定值复核，设备上经 admission 约束 `native_target == 当前执行区基址` 三方一致。

## 4. 验证证据

### 4.1 host 静态/模拟（真实设备端翻译单元链接）

| 套件 | 结果 |
| --- | --- |
| 新: `tests/app_host_run`（编排核心：参数/未装/原始返回码 7/-3/0/运行中标志/互斥 BUSY 双路径/损坏/缺文件/短文件/锚缺失/外来钥/资源限/身份篡改/记录撕裂/stat·读 IO 未知/卸载后/后端两级失败/重启零自启+显式再运行/日志截断） | **174 checks, 0 failures** |
| `tests/app_installer`：事务核心 271/0、故障注入 280/0、存储后端 54/0（既有不回退） | 全绿 |
| `tests/app_installer` gate（真实 handler + mongoose b64）：87→**154 checks, 0 failures**（新增 run 门/方法/生命周期/BUSY/各拒绝路径/重启语义/`app_running` 字段/路由数 4） | 全绿 |
| `tests/app_admission` 179/0 + 85/0；`tests/storage_gate` 802/0 + 267/0 + 1960/0 + 83/0；`tests/app_host` 21/0；sign spike 22/22 | 全绿 |
| 锚守卫 `anchor_keygen.py`：`06e732b1…` | 退出码 0 |
| 向量再生（托管钥，RFC6979 确定性）：既有向量字节不变（diff 纯加性 28 行），新增 ABI 失配包 `ea23ec94…`（版本 1.2.0，abi=0x00010001） | 完成 |

**验收负例对照**：未认证/默认口令高危门（gate 403 先于 body）✓；原生裸文件/单独上传（入口无 path/上传参数，负例为结构性不存在）✓；损坏签名（`SIGNATURE_INVALID` 透传）✓；换钥失信（锚字节翻转 → `PUBLISHER_UNTRUSTED`）✓；ABI 失配（专用签包向量 `HELLO_BADABI_PACKAGE`，安装与运行重验双拒）✓；未提交（NOT_INSTALLED）✓；`unavailable`（UNAVAILABLE+detail）✓；`STORAGE_STATE_UNKNOWN`（记录撕裂/stat/读 IO 三路径）✓；运行中再执行/升级/卸载（BUSY 三向）✓；重启无自启（双套件断言 invoke_count==0 且标志 FALSE）✓。

### 4.2 目标固件（交叉编译证据，非运行时证据）

- `rm -rf Appli/build && make app -j16`（默认 `APP_HOST_POC=0`）→ 完成；`nm` 确认 `app_host_run*`/`app_install_run_handler` 符号为 0。
- `rm -rf Appli/build && make app APP_HOST_POC=1 -j16` → 完成；`nm` 确认 `app_host_run_installed`/`app_host_run_device_backend`/`app_install_run_handler` 留存于 ELF。
- 工具链: Arm GNU 13.3/15.x（见构建日志）。

## 5. NOT_RUN 与能力边界（诚实声明）

| 项 | 状态 | 原因 |
| --- | --- | --- |
| 真机手动运行端到端（安装→run→UART 日志→返回码） | NOT_RUN | 无设备写入/运行授权 |
| 真机并发互斥（Web run 与安装/卸载/UART 竞态实测） | NOT_RUN | 同上；host 以同一事务锁语义模拟 |
| 真实原生 App 崩溃/挂起对 Host 的影响 | NOT_RUN | 协议明确不承诺强制终止/沙箱/崩溃隔离；无授权实测 |
| HTTP 状态码最终映射 | 待 P5 | 当前 200/400/403/405 为本实现选择 |
| `log` 捕获与 UART 输出在真实入口下的完整性 | NOT_RUN | 仅 host 桩与代码审查证据 |

## 6. 复现指引

```sh
python3 Script/version_header.py --fsbl-version 1.0.3.1 --stedgeai-version 3.0.0
cd tests/app_host_run && make test          # 174 checks
cd ../app_installer && make test            # 271 + 280 + 154 + 54
cd ../app_admission && make test            # 179 + 85
cd ../storage_gate && make test             # 802 / 267 / 1960 / 83
cd ../app_host && make test && cd ../sign_spike && make
cd ../.. && rm -rf Appli/build && make app -j16                 # 默认镜像
rm -rf Appli/build && make app APP_HOST_POC=1 -j16              # 实验 PoC 镜像
python3 Script/gen_web_api_docs.py --check                      # 提交后 OK
python3 tests/app_installer/sign_packages.py --key ~/.ne301-keys/ne301-app-issuer-v2.sec1.pem --foreign-key ~/.ne301-keys/ne301-fixture-reject-foreign.sec1.pem
```
