# App Admission v2 — 受信验签准入与能力版本分派（#47 设计说明）

本记录中的 App 签名与发行者信任链已由 #136 退役。当前 Host 格式和安装策略见 [NEAPP05 无签名单文件格式](./app-unsigned-single-file-format.md)。

状态：#47 Candidate 实现 说明文档（非规范；唯一协议权威仍是 ne30x-app `docs/app-package-protocol-v2-draft.md`，blob `c9457cfeae95ea0ff6f357818b311bba13a94a60`）。

## 1. 分派模型

`app_admission_verify_package`（`Custom/Services/AppHost/app_admission.c`）改为按容器 magic 严格分派：

- `4e45415050010000`（v1）→ `ad_verify_v1`：原 #29 v1 全部判定逐字节保持（160B manifest、`0x00010000` ABI、16B API table、仅 bit0/bit1 已知能力、单次运行语义）。
- `4e45415050020000`（v2）→ `ad_verify_v2`：新增 v2 路径。
- 其它 magic / 短包 → `BAD_PACKAGE`。

两条路径互不借用：v1 解析器永远不会解释 v2 manifest（域隔离），v2 路径也不接受任何 v1 取值（manifest `required_host_abi` 必须 `0x00020000`、manifest magic 必须 `NMF2`、`run_profile` 必须为 1）。

## 2. v2 判定序（继承 v2 §2.3 / v1 §7.1）

1. 有界结构：`manifest_len==192`、`image_len>=32` 且 ≤ 2 MiB、`16+192+image_len+8 <= len <= min(2097432, policy->max_package_len)`、DER 段 8–72B。
2. manifest 结构（`BAD_PACKAGE`）：`NMF2`、publisher/app 标识符字符集与右零填充、`[58..59]` 保留零、`required_host_abi==0x00020000`、`run_profile==1`、`[176..191]` 保留零、caps ⊆ bit0..5（bit6..31 任一置位拒绝）、能力↔配额一一对应（bit2⇔`event_max>0`、bit3⇔`report_max>0`、bit4⇔`state_quota>0`）、profile 1 必须含 bit5（session_lifecycle）。
3. 可信发行者：独立 Host 唯一 `publisher_id → SHA-256(SPKI DER)` 映射（复用 v1 单钥信任锚语义）；包内 `publisher_key_sha256` 与 manifest publisher 必须与 Host 映射一致，包自带键/指纹不可自授信。
4. 验签：对原样 `TBS=[0:16+192+image_len]` 的 SHA-256/ECDSA P-256 严格 DER（非最短编码/负值/零值/越界/尾随/歧义一律 `BAD_PACKAGE`；验签不过 `SIGNATURE_INVALID`）。
5. 策略：原生头/摘要/CRC/长度交叉检查（`BAD_PACKAGE`，manifest ABI ≠ 原生头 ABI 拒绝）→ 板型/PSRAM/target（`TARGET_INCOMPATIBLE`）→ ABI 与 caps 支持（`ABI_INCOMPATIBLE`）→ 装载/执行区（`RESOURCE_LIMIT`）→ 配额容量（见 §3）。

幂等内容身份沿用 `SHA-256(TBS)`：不同但均合法的 DER 签名（含 high-S 变体）产出相同 `tbs_sha256`，不构成版本冲突（继承 #30 r2 语义）。

## 3. 能力门槛与实测容量（签名声明 ≠ Host 授权）

v2 能力位：bit0=log、bit1=tick_ms、bit2=ai_events、bit3=report_submit、bit4=app_state、bit5=session_lifecycle；bit6..31 非法。`policy->supported_caps_mask` 表达 Host 当前**实际提供**的能力面：

- 当前真实设备策略（`api_app_admission_module.c` / `api_app_install_module.c`）保持 `abi=0x00010000`、`caps=0x3`（log/tick）不变。合法 v2 包必然要求 bit5，因此当前 Host 上任何 v2 包都被 `ABI_INCOMPATIBLE` 拒绝——这正是"设备实际未提供能力时拒绝"的 fail-closed 行为，`caps=0x3f` 黄金包不会被判定为可安装/可运行。
- 策略结构体新增三个**实测容量**字段：`event_capacity` / `report_capacity` / `state_quota_capacity`（0=未提供）。仅当对应能力位被请求且 Host 支持时才检查：
  - `event_max` 必须 ≥ 1576（40B 事件头 + 64×24B 检测记录，Line Crossing 64 框业务常量）且 ≤ `event_capacity`；
  - `report_max` 必须 ≥ 6144（`LC_DQ_SLOT_CAPACITY`，完整合法报告不截断）且 ≤ `report_capacity`；
  - `state_quota` ≤ `state_quota_capacity`。
  - 违反 → `RESOURCE_LIMIT`（含 §8 M7 归档 1024/4096 样本）。
- 装载量验证使用策略传入的实测 `exec_region_size`（设备侧来自 `app_host_exec_region` 链接符号），不以签名声明或硬编码 2 MiB 认定。

"全部功能可用时的准入判定"由测试夹具策略（abi=0x00020000、caps=0x3f、容量 2048/6144/4096）证明；该夹具只存在于测试，不接入设备策略。#43/#44/#46 的运行时功能不在本 Issue 内。

## 4. 与 #30 安装事务的协作

`app_installer.c` 仅调整参数上界为 `APP_ADMISSION_MAX_PACKAGE_LEN_V2`（2097432，v1/v2 结构上限的并集），精确的按版本上限仍由分派后的 admission 判定；事务、幂等、降级、换钥、`STORAGE_STATE_UNKNOWN` 语义零改动。API 模块（b64 上界 2097400、设备策略 caps=0x3）未改动——超界 v2 包在 API 入口即被拒（fail-closed，不产生歧义语义）。

## 5. 测试映射（tests/app_admission/test_app_admission_v2.c，75 checks）

- 正例：#12 §10.1 黄金包逐字节（`gen_v2_vectors.py` 生成 `v2_vectors.h`，断言包/TBS/SPKI SHA-256 与跨仓证据一致：`3766e8af…`/`e14e2cce…`/`5cd252fb…`）；RFC6979 重签、high-S 变体（异合法 DER 同 `SHA-256(TBS)`）。
- 域隔离：黄金包 vs 当前设备策略（abi 0x10000/caps 0x3）→ `ABI_INCOMPATIBLE`；黄金包 vs abi 0x20000 但 caps 0x3 → `ABI_INCOMPATIBLE`（M10 反向 fail-closed）；v1 黄金包在当前策略照旧通过且 verdict 不携带任何 v2 字段（M3：v1 只能按 v1 单次运行）。
- 容量：event/report/state 任一实测容量不足 → `RESOURCE_LIMIT`；归档 1024/4096 → `RESOURCE_LIMIT`（M7）；装载区 35B < `native_file_len` 36B → `RESOURCE_LIMIT`。
- 负例矩阵（§8 M1–M10 + §10.3）：未知 magic、`manifest_len≠192`、`NMF1` 混搭、manifest abi=0x10000（M4）、`[58..59]`/`[176..191]` 保留非零、未知 profile、profile 1 缺 bit5、bit6（M5）、能力↔配额双向不匹配（M9）、DER 尾随/截断、`image_len<32`、整包截断、篡改 TBS/DER、原生头 ABI 错配（M8，重签+摘要同步）、原生头 reserved0、错板/超 PSRAM/异 target（重签合法仍拒）、自声明密钥重签仍 `PUBLISHER_UNTRUSTED`、外来钥签名 `SIGNATURE_INVALID`、信任缺失 `TRUST_UNPROVEN`。
- v1 回归：tests/app_admission 179+85 checks 不回退；tests/app_installer 271+280+87+54、tests/storage_gate 802+267+1960+83、tests/app_host 21 全绿。

## 6. 边界（无 PASS 声明）

宿主向量不证明 STM32 目标 PKA/mbedTLS 设备验签、真实掉电恢复、持续会话运行或真实 v2 App 启动；执行入口/会话管理属 #31/#43。当前固件默认构建（APP_HOST_POC=0）不包含 AppHost 符号。
