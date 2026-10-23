# NE301 可信 App 安装/升级/卸载持久事务（Issue #30 实施设计）

本记录中的 App 签名与发行者信任链已由 #136 退役。当前 Host 格式和安装策略见 [NEAPP05 无签名单文件格式](./app-unsigned-single-file-format.md)。

- Issue: harryhua-ai/ne301#30（`ght-contract` AC1–AC4）
- 基线: `experiment/app-host-poc@eb0a97cfdcbcc8ba7d71b91521c447a22d550b6b`（含 #29 P2 v1 验签准入与 #37 存储安全基线）
- 协议权威: [ne30x-app `docs/app-package-protocol-v1.md`](https://github.com/harryhua-ai/ne30x-app/blob/main/docs/app-package-protocol-v1.md)（P2 v1 冻结）；本文与其冲突时以该规范为准
- 本文档性质: B 实施记录（HOW），不是协议权威，不是设备/生产安全认证

## 0. 结论速览

| 交付物 | 位置 | 状态 |
| --- | --- | --- |
| 安装/升级/卸载/查询持久事务核心（纯逻辑，host 可测，注入 IO + 注入验签） | `Custom/Services/AppHost/app_installer.{c,h}` | 完成，host 157/0 + 故障注入 174/0 |
| 设备存储后端（LittleFS 包文件 + NVS USER 记录/意图标记 + 事务互斥锁） | `Custom/Services/AppHost/app_installer_storage.c` | 完成（仅 `APP_HOST_POC=1` 构建） |
| 管理员高权限 API（install / installed 查询 / uninstall） | `Custom/Services/Web/api/api_app_install_module.{c,h}` | 完成，host gate 66/0 |
| 签包工具与 hello-app 测试向量（绑定当前编译锚） | `tests/app_installer/sign_packages.py` + `installer_vectors.h` | 完成，锚指纹 `06e732b1…` 逐字节断言 |
| 固件接线（web_service 注册、Appli Makefile、实验加载器运行态标记） | `Custom/Services/Web/web_service.c`、`Appli/Makefile`、`Custom/Services/AppHost/app_host.c` | 完成，默认构建无安装器符号（nm 证据） |
| 宿主测试套件 | `tests/app_installer/` | 157 + 174 + 66 checks 全绿 |
| 目标固件新鲜构建 | 默认 + `APP_HOST_POC=1` 两个镜像 | 均通过 |

**明确不做**（non-goals 遵守）：不重复实现第二套验签/信任规则（安装路径直接复用 `app_admission_verify_package` + 编译期锚）；不改 Host ABI/分区表/FSBL/OTA/Web 资产；不做 Web 前端、App 执行调度、多 App 并存、强制终止、v2/v3 能力；不执行任何真实刷写/掉电/分区操作（无授权，见 §7 NOT_RUN）。

## 1. 存储模型与数据流

```
HTTP POST /api/v1/app/install            (仅 APP_HOST_POC=1 构建注册)
  │ require_auth 路由 + web_server_high_privilege_allowed()   ← #36 判定, 先于一切解析
  ▼
api_app_install_module.c   base64 有界解码 → api_app_install_fill_ctx()
  │   （锚自检 status==provisioned → TRUST_UNPROVEN fail-closed；策略=board/PSRAM/ABI/caps/执行区）
  ▼
app_installer.c  app_install(io, verify, ctx, package, len)
  1) verify() = #29 app_admission_verify_package（真实编译锚）→ 拒绝类直接透传，零写入
  2) 事务互斥 try-lock（设备=CMSIS mutex / host=注入）→ BUSY；app_install_app_running()=TRUE → BUSY
  3) 惰性 boot resolve（首次任何操作前，解析遗留意图标记）
  4) 读已提交记录（NVS，CRC 包裹）→ 不可读/坏 CRC → STORAGE_STATE_UNKNOWN，拒绝更改
  5) 身份/版本裁决（见 §3 矩阵）
  6) 容量预检：free ≥ 2×package_len（保守配额，含 COW/元数据余量）→ STORAGE_FULL
  7) 暂存=直接写最终内容寻址路径 + 全量读回 SHA-256 比对 + 再次完整验签
  8) 写前意图标记（NVS，CRC 包裹，写+读回证明）
  9) 写新记录（写+读回证明）
 10) 提交点 = 证明性清除意图标记；清理旧版本文件（尽力而为，孤儿文件无身份不冒充）
  ▼
GET /api/v1/app/installed   → 读记录 + stat + 全量读回 SHA 比对 + 当前锚/策略完整复验
                              （缺失/损坏/失信/不兼容 → 保留身份呈现 unavailable + 原因）
DELETE /api/v1/app/installed → 意图标记 → 记录删除（证明）→ 提交 → 文件 GC
```

- **包文件**: `/apps/pkg/<SHA-256(整包) hex>.neapp`（内容寻址）。同 TBS 不同签名字节只会落不同路径；记录只引用"整包读回验证通过"的路径，任何半写文件永不具有已提交身份。
- **记录**（NVS USER `app_install_record`，296B，CRC32 包裹）: magic/版本 + publisher_id(17)/app_id(33) + 版本 3×u16 + package_len + package_sha256/tbs_sha256/image_sha256/publisher_key_sha256（4×32）+ package_path(96 槽) + CRC。
- **意图标记**（NVS USER `app_install_pending`，312B，CRC32 包裹）: magic + action('I'/'U') + has_prev + 前记录全量内嵌 + CRC。
- **共享 LittleFS**: 遵循 #30 存量核查结论——业务 App 走共享卷，不改 APP1/APP2/分区表；`/captures` 等用户文件不碰（测试有专项断言）。

## 2. 写前意图事务纪律（AC3，与 #36/#37 同型）

1. **标记先于记录**: 意图标记（含前记录全量）以"写+读回逐字节比对"证明落盘；写不成但可证明缺席 → 清理本次新文件并报 `WRITE_FAILED`（all-old）；写不成且无法证明缺席 → `STORAGE_STATE_UNKNOWN`（fail-closed，本会话拒绝一切更改）。
2. **记录写+读回证明**: 新记录写失败 → 证明性恢复前记录（或证明性删除），证明性清标记 → `COMMIT_FAILED`（all-old）；任一步不可证明 → `STORAGE_STATE_UNKNOWN`。
3. **提交点 = 证明性清标记**: 记录已写但标记清不成 → `STORAGE_STATE_UNKNOWN`，不做内联回滚，留给 boot 解析（语义与 #36 管理员凭据提交一致）。
4. **boot 解析标记**（`app_install_boot_resolve`，注册时执行一次 + 首次操作惰性兜底）:
   - 标记缺席 → 无事；
   - 标记存在且 CRC/字段有效 → 依 has_prev 恢复前记录（写证明）或证明性删除记录，再证明性清标记；
   - 标记不可读/CRC 坏 → **fail-closed**：置会话未知态，查询返回 `unknown`，安装/卸载一律拒绝，绝不冒充未安装、绝不清数据，标记原样保留（下次 boot 重试）。
5. **卸载窗口**: 标记(action=U) → 记录删除（证明）→ 提交 → 文件 GC。掉电落在任何中间点，boot 恢复为"仍安装旧版本"或"已卸载"，两者都是完整状态。

## 3. 身份、版本与冲突裁决（AC2）

- 逻辑身份 = `publisher_id + publisher_key_sha256 + app_id`（均来自验签后的 manifest，且发行者指纹已对当前锚恒时校验）。
- **幂等**: 同版本且 `SHA-256(TBS)` 相同 → `ALREADY_INSTALLED`；目标文件缺失/损坏时同路径重暂存修复（修复路径同样全量读回+复验）。
- **同版本不同内容**（TBS 不同）→ `VERSION_CONFLICT`；**降级**（候选版本 < 已安装）→ `DOWNGRADE_REFUSED`；**跨发行者**（已安装记录 publisher ≠ 候选 publisher，含锚轮换遗留）→ `PUBLISHER_CONFLICT`；**不同 app_id**（单槽位被其他身份占用）→ `IDENTITY_CONFLICT`，须先显式卸载。以上全部零写入（记录与文件集逐字节不变，测试断言）。
- **坏包/不可信/不兼容候选**: 验签/策略拒绝发生在任何写之前；已验证当前版本不可能被静默替换。
- **已知运行中 → BUSY**: 实验加载器（`app_host_load_and_run`）进入 App 入口前置位 `app_install_set_app_running(TRUE)`、返回后清零；运行中安装/卸载一律 `BUSY`，不推断任何强制终止能力。

## 4. 查询语义（AC1）

`GET /api/v1/app/installed` 只呈现**实际已提交**的记录：`state=none|installed|unavailable|unknown`。installed/unavailable 都带 publisher/app/版本/package_len/package_sha256/tbs_sha256；unavailable 另带原因（`file_missing`/`content_mismatch`/`content_unreadable`/验签拒绝类如 `PUBLISHER_UNTRUSTED`/`TARGET_INCOMPATIBLE`/`TRUST_UNPROVEN`）——身份保留，绝不因失信或损坏冒充"未安装"。上传/暂存/验证均不产生 installed 状态；install 响应仅在提交完成后返回 `"status":"installed"`。

## 5. API 与权限（AC1）

- `POST /api/v1/app/install`（body `package_base64`，上限同 #29 的协议导出值 2,800,000 字符）
- `GET /api/v1/app/installed`
- `DELETE /api/v1/app/installed`
- 三条路由 `require_auth=AICAM_TRUE` 且 handler 第一步再调 `web_server_high_privilege_allowed(ctx)`（与 `system_factory_reset_handler` 同型，深防御）；403 分支不读取、不解析 body。响应统一 200 + JSON（`status: installed|already_installed|uninstalled|refused` / `state: none|installed|unavailable|unknown`），认证失败 403，载荷非法 400，方法不符 405（HTTP 状态码终映射仍待 P5 冻结）。注册时执行存储后端初始化（互斥锁 + `/apps`、`/apps/pkg` 目录）与一次 boot 意图解析。

## 6. 验证证据与边界（AC4）

### 6.1 host 静态/模拟（本轮实际执行，全部真实设备端翻译单元链接）

| 套件 | 命令 | 结果 |
| --- | --- | --- |
| 新: 事务核心 | `cd tests/app_installer && make test`（`test_app_installer`） | **157 checks, 0 failures** |
| 新: 故障注入 | 同上（`test_app_installer_faults`） | **174 checks, 0 failures** |
| 新: API 门 | 同上（`test_app_install_gate`，真实 handler + mongoose b64） | **66 checks, 0 failures** |
| 基线: 协议核心/门 | `cd tests/app_admission && make test` | 179/0, 85/0 |
| 基线: storage gate | `cd tests/storage_gate && make test` | 802/0, e2e 267/0, admin 1960/0, boundary 83/0 |
| 基线: app_host / sign spike | `make test` / `make` | 21/0, 22/22 |
| 锚绑定守卫 | `python3 tests/app_admission/anchor_keygen.py` | 退出码 0，编译锚指纹 `06e732b1…` |
| 签包再生（托管钥） | `python3 tests/app_installer/sign_packages.py --key ~/.ne301-keys/ne301-app-issuer-v2.sec1.pem --foreign-key ~/.ne301-keys/ne301-fixture-reject-foreign.sec1.pem` | RFC6979 确定性，重复运行 `installer_vectors.h` sha256 一致 |
| docs-sync | `python3 Script/gen_web_api_docs.py --check` | 提交后 OK |

- **签名包供给**: `sign_packages.py` 从仓库外托管 SEC1 私钥（`~/.ne301-keys/ne301-app-issuer-v2.sec1.pem`，0600）以 RFC 6979 + low-S 重签 hello-app 测试包；工具**首先断言签名钥 SPKI SHA-256 逐字节等于当前固件锚指纹** `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`，不符即退出——旧 P3 dev-publisher 或旧失效锚签的包无法产出本套件向量。私钥与派生逻辑绝不入库；入库的只有包字节/摘要（hello-app v1.0.0 `f98811f8…`、v2.0.0 `2a86a298…`、v2.0.0 异内容 `c431baf8…`、错板 `f5eecda2…`、外来钥 `fd1ceacd…`，TBS 摘要见向量头）。
- **事务核心套件**（真实 `app_installer.c` + `app_admission.c` + `app_trust_anchor.c` 链接，注入内存 IO）: 记录编解码往返/CRC/坏 magic；全新安装→查询→幂等→V1→V2 升级（旧文件 GC）→降级拒绝→同版异内容拒绝→卸载→重复卸载 NOT_FOUND；篡改包/错板/外来钥/截断包/NULL 参数拒绝且零写入；跨发行者与异身份冲突；互斥 BUSY 与运行态 BUSY；查询 unavailable（缺文件/内容损坏/记录指向异内容）；信任失效拒绝且零写入；用户文件 `/captures` 全程不变；结果/状态字符串表。
- **故障注入套件**（同一后端加逐操作失败计数器 + 定向键失败 + CRC 撕裂直写 + 重置会话模拟重启）: 暂存写失败/读回失败保持旧版本；容量不足 `STORAGE_FULL` 且零写入；标记写不成可证缺席 → 回滚 all-old；标记写不成不可证缺席 → UNKNOWN → 重启解析恢复；记录写失败证明回滚 `COMMIT_FAILED`；提交标记清不成 → UNKNOWN → boot 解析回滚旧版本；首次安装中途掉电 → boot 清理不冒充；标记撕裂（CRC 坏）→ fail-closed（查询/安装/卸载全拒，标记保留）；标记不可读 → UNKNOWN → 可恢复；记录不可读/撕裂 → UNKNOWN；卸载三个掉电窗口（写标记前/提交前/清理前）各自恢复语义；重启后已提交身份可查询；损坏已装版本同内容重装修复；提交回滚孤儿文件被后续安装原地复用不重复；卸载后同版本重装；故障全程用户数据不变。
- **API 门套件**（真实 `api_app_install_module.c` + mongoose b64 + 注入 IO）: 未认证/仅认证/高权限组合 403 且 body 不读；方法不符 405；载荷负例（坏 JSON/缺字段/非 base64/超限）400；install→installed→查询→升级→幂等→冲突→降级→卸载→none 全链路 JSON 断言（含包摘要/TBS 摘要逐字节）；运行态 BUSY；路由注册 3 条。
- 上述全部为 **macOS host 模拟**，非设备运行证据。

### 6.2 目标固件（交叉编译证据，非运行时证据）

- `rm -rf Appli/build && make app -j16`（默认 `APP_HOST_POC=0`）→ 完成；text 3,460,624B；`nm` 确认 `app_admission_*`/`app_trust_anchor_*`/`app_installer_*`/`app_install_*` 函数符号为 0（仅链接脚本 `__app_host_ram_*` 存在，与基线一致）。
- `rm -rf Appli/build && make app APP_HOST_POC=1 -j16` → 完成；text 3,488,664B（默认 +28,040B，安装栈 + 被引用 mbedTLS 路径）；`nm` 确认 `app_install`/`app_install_boot_resolve`/`app_install_dev_*`/三个 API handler 及 `mbedtls_ecdsa_verify` 留存于 ELF。
- 工具链: Arm GNU 15.2.Rel1（`arm-none-eabi-gcc 15.2.1`）。
- **未做**: 任何真机烧写/串口/运行时验证（见 §7）。

## 7. NOT_RUN 与能力边界（诚实声明）

| 项 | 状态 | 原因 |
| --- | --- | --- |
| 真实设备安装/升级/卸载端到端 | NOT_RUN | 无设备写入授权（契约：真实刷写/分区操作需另行满足设备/备份/恢复前提与 User 明确许可） |
| 真实物理掉电/写失败恢复测试 | NOT_RUN | 同上，不得用宿主模拟冒充真机 PASS |
| 主固件/基础 Web 分区内容核对（安装前后） | NOT_RUN | 同上；设计上安装仅写共享 LittleFS `/apps/pkg` 与 NVS USER 两键，不触碰 APP1/APP2/FSBL/WEB/AI 分区 |
| LittleFS 真实卷容量/碎片/缓存行为 | NOT_RUN | `STORAGE_FULL` 预检用保守 2× 配额 + `storage_get_disk_info`（有短期缓存），真实边界待有授权设备实测 |
| HTTP 状态码最终映射 | 待 P5 | 当前 200/400/403/405 为本实现选择，不预设规范结论 |
| 多 App 并存/并发调度/App 执行沙箱 | non-goal | 契约 non_goals；单槽位模型是本轮诚实边界 |

## 8. 复现指引

```sh
python3 Script/version_header.py --fsbl-version 1.0.3.1 --stedgeai-version 3.0.0
cd tests/app_installer && make test     # 157 + 174 + 66 checks，退出码即结果
cd ../app_admission && make test        # 179 + 85
cd ../storage_gate && make test         # 802 / 267 / 1960 / 83
cd ../app_host && make test && cd ../sign_spike && make
cd ../.. && rm -rf Appli/build && make app -j16                 # 默认镜像（无安装器代码）
rm -rf Appli/build && make app APP_HOST_POC=1 -j16              # 实验 PoC 镜像（含安装器）
python3 Script/gen_web_api_docs.py --check                      # 提交后 OK
```

## 9. 遗留与建议

1. 端点页 `docs/web-api/endpoints/app_install.md`（en/zh）模块标题回退为键名（`Script/gen_web_api_docs.py` `MODULE_META` 未加条目——`Script/**` 不在本任务 scope）；后续任务可补元数据。
2. `installer_vectors.h` 再生须在持有托管私钥的受控环境执行（见 §6.1 命令）；工具自带锚指纹断言，任何锚轮换会使旧向量无法再生成（预期 fail-closed）。
3. 安装目录 `/apps/pkg` 中未被记录引用的孤儿文件（掉电窗口产物）惰性保留、无身份不冒充；如需空间回收可在后续受权任务中加显式 GC。
