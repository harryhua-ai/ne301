# NE301 可信 App 验签与管理准入边界（Issue #29 实施设计）

本记录中的 App 签名与发行者信任链已由 #136 退役。当前 Host 格式和安装策略见 [NEAPP05 无签名单文件格式](./app-unsigned-single-file-format.md)。

- Issue: harryhua-ai/ne301#29（`ght-contract` AC1–AC4）
- 基线: `experiment/app-host-poc@5683736dca30a7c3349678f9fcb9ce5c6c33a750`（含 #37 存储安全基线与 #36 管理员认证基线）
- 协议权威: [ne30x-app `docs/app-package-protocol-v1.md`](https://github.com/harryhua-ai/ne30x-app/blob/main/docs/app-package-protocol-v1.md)（已冻结 P2 v1，blob `c7df6e79e53845c6a08416e9940b84fa4e19fbaf`）；本文与其冲突时以该规范为准
- 本文档性质: B 实施记录（HOW），不是协议权威，不是设备/生产安全认证

## 0. 结论速览

| 交付物 | 位置 | 状态 |
| --- | --- | --- |
| P2 v1 包解析/验签/准入判定核心（纯逻辑，host 可测） | `Custom/Services/AppHost/app_admission.{c,h}` | 完成，host 179/179 |
| 设备信任锚（编译期内置、单发行者、无变更接口、fail-closed 自检、受控签名者配对证明） | `Custom/Services/AppHost/app_trust_anchor.{c,h}` + `tests/app_admission/anchor_keygen.py` | 完成 |
| 管理员高权限 API 入口（verify / state，复用 #36 判定） | `Custom/Services/Web/api/api_app_admission_module.{c,h}` | 完成，host gate 85/85 |
| 实验 UART 装载默认关闭（boot 级 arm/disarm 门） | `Custom/Services/AppHost/app_host.c` | 默认 disarmed |
| 固件接线（仅 `APP_HOST_POC=1` 且 64MiB PSRAM 构建） | `Appli/Makefile`、`Custom/Services/Web/web_service.c` | 默认构建无准入代码 |
| host 测试套件（P2 黄金/负例向量 + 门负例 + 编译锚正/错配证据） | `tests/app_admission/` | 179 + 85 checks 全绿 |
| 目标固件新鲜构建 | 默认 + `APP_HOST_POC=1` 两个镜像 | 均通过；准入符号仅在 PoC 镜像中留存 |

**明确不做**（non-goals 遵守）：不执行任何上传文件；无安装清单/版本提交/卸载/掉电恢复/UI；不产生 `installed` 状态；`verified`/准入结果不进入任何执行路径；不改 OTA/FSBL/分区表/公共 ABI；不把 NVS/LittleFS/Web 当信任锚写入口。

## 1. 模块与数据流

```
HTTP(S) POST /api/v1/app/admission/verify        (仅 APP_HOST_POC=1 构建注册)
  │ require_auth 路由 + web_server_high_privilege_allowed()   ← #36 判定, 先于一切包解析
  ▼
api_app_admission_module.c   base64 解码(有界) → 策略(板型号/PSRAM/ABI/caps/执行区) + 设备信任锚
  ▼
app_admission.c  app_admission_verify_package()
  1) 容器结构: magic NEAPP v1 / manifest_len==160 / image_len 有界 / 溢出安全 / DER 尾段 8..72B 全消费
  2) manifest 唯一编码: NMF1 / 标识符字符集 / reserved 全零 / caps 未知位必须零
  3) 发行者绑定: manifest.publisher_key_sha256 == SHA-256(设备 SPKI DER)（恒时比较）
                  且 manifest.publisher_id == 锚 publisher_id
  4) 严格 DER 解析 + ECDSA P-256 验签(mbedTLS) over SHA-256(TBS)，r/s∈[1,n)
  5) 签名后策略: native 摘要恒时比较 / CRC32 / 32B 原生头 v1 严格化(header_size==32、
     reserved0==0、NEA1、format==1、manifest↔头逐字段一致、entry Thumb 对齐)
  6) 兼容矩阵: board/PSRAM → TARGET_INCOMPATIBLE；ABI/caps → ABI_INCOMPATIBLE；
     执行区/装载容量 → RESOURCE_LIMIT
  ▼
仅输出 verdict JSON（含 "executable":false, "installed":false）；不落盘、不装载、不执行
```

错误主类与 P2 §7.1 对齐：`BAD_PACKAGE` / `SIGNATURE_INVALID` / `PUBLISHER_UNTRUSTED` / `TARGET_INCOMPATIBLE` / `ABI_INCOMPATIBLE` / `RESOURCE_LIMIT`，另加本地类 `TRUST_UNPROVEN`（锚缺失/无法证明保护状态 → fail closed）与 `PARAM`。权限失败（`AUTH_REQUIRED`/`FORBIDDEN` 语义）由 #36 的 `web_server_high_privilege_allowed` 在任何解析前给出，未授权调用者得不到发行者/密钥/解析细节。HTTP 状态码映射留待 P5（规范明示待冻结），当前 verify 结果统一 200 + verdict JSON，认证失败 403。

## 2. P2 协议符合性要点（AC1）

- **容器**: 仅 `4E 45 41 50 50 01 00 00` magic；两个 u32-le 长度做 `uint64` 溢出安全加法；`manifest_len` 唯一合法值 160；`image_len ≥ 32` 且 ≤ 2,097,152；整包 ≤ 2,097,400；签名段长度 ∈ [8,72] 且整文件恰好消费（多余尾字节、截断、零长度、第二个 DER 一律 `BAD_PACKAGE`）。
- **TBS**: 包首 `16 + manifest_len + image_len` 字节原样作为签名原文；幂等身份使用 `SHA-256(TBS)`，整文件摘要仅作诊断（未用于版本判断——本任务无安装记录可比）。
- **manifest**: 定长 160B；`NMF1`；`publisher_id`(16B)/`app_id`(32B) 规范 ASCII 小写标识（首字符 a-z、字符集 [a-z0-9-]、禁首尾 `-`、禁内嵌 NUL、禁非零右填充、恰好满槽允许）；reserved 两字节必须全零；`required_host_caps` 未知位必须零（编码级，`BAD_PACKAGE`）。
- **签名**: SHA-256 + ECDSA over secp256r1；自写严格 DER 解析器（定长、最短长度编码、`ECDSA-Sig-Value` 恰好两个正 INTEGER、无尾随数据、r/s 以 mpi 范围复核 `1 ≤ r,s < n`），验签调用 vendored mbedTLS 3.6.4（与固件同源）软件路径；**不要求 low-S**（high-S golden 与 low-S 派生包均须通过，与 §2.5 一致）。
- **原生镜像**: 32B 头 v1 严格化在签名验证之后执行——`header_size` 恰为 32（比现有 loader 的 `>=32` 更严，不改 loader 行为）、`reserved0==0`、`NEA1`、`format_version==1`、`CRC32`(反射 IEEE，`generic_crc32`) 独立校验、manifest 与头的 `image_size/entry_offset/target_addr/abi` 逐字段一致、`native_file_sha256` 恒时比较、`native_file_len == image_len == 32 + native_image_size`。
- **装载/驻留双容量**（§2.3）: `required_exec_region_bytes ≥ native_image_size`、`≤ 设备执行区`，且 `native_file_len ≤ 设备执行区`，缺一即 `RESOURCE_LIMIT`；golden 的 4B 驻留声明与 36B 装载量分别成立，不互相替代。
- **向量消费**: `tests/app_admission/p2_vectors.h` 由规范 §2.5 字节逐字转录（生成时用 `sha256sum` 对规范公布的三组 SHA-256 全部核对通过：golden 包 `9a212324…`、TBS `5035e651…`、d=1 SPKI `5cd252fb…`）。消费覆盖：golden 正例、low-S 派生正例、70B DER 正例、`golden+1B` 拒绝、70B+`FF` 尾拒、`reserved0!=0` 已签名包拒绝、7 个固定已签名负例（`manifest_uppercase_app_id`/`manifest_reserved_nonzero`/`wrong_board_signed`/`untrusted_key_fingerprint`/`native_header_size_33`/`native_entry_offset_oob`/`mismatched_manifest_native_len`）逐一复现规范预期类别，并先断言每个负例 TBS 摘要与规范一致、签名本身在 host OpenSSL 互操作下有效（拒绝必须来自编码/策略检查而非签名失败——测试内验证过）。
- **错误码优先级**: 按 §7.1 判定顺序实现（有界结构 → 发行者 → 验签 → 签名后策略 → 兼容/资源）；复合坏包除先决关系外不承诺字段级优先级。

## 3. 信任锚生命周期（AC3）

### 3.1 供应与形态

- 唯一信任锚为**编译期内置**的 `static const` 数据：`format_version=1`、单一 `publisher_id="ne301-exp-pub"`（13B，规范字符集）、发行方 P-256 公钥 **SPKI DER 91B**、及其 SHA-256 指纹常量。
- **锚公钥指纹（写入固件的非生产实验钥，本轮轮换后）**: `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`。SPKI DER HEX 记录于 `Custom/Services/AppHost/app_trust_anchor.c`，对应受控发行者私钥按 §3.1a 托管于仓库之外。**私钥不入库**——仓库与固件仅含公钥半，且仓库内不存在任何可复现该私钥的算法、种子或标签；该密钥明确定位为受控实验 Host 的非生产测试信任锚，不具任何生产发行效力。
- **轮换记录（原随机锚 → 确定性夹具锚）**: 上一轮实现以 `openssl ecparam -genkey`（非确定性）生成了原锚（指纹 `f5ca101c1a3cb2072db7ae4b000c81bec6557936db74cc2f6f68a0353e1fd031`），其私钥半按"不入库"约定未持久托管，现已不可恢复——导致该锚**在数学上不可能**产生任何匹配签名正例。该轮曾按 §3.3 第 2 条的既定轮换规则（"更换/撤销 = 另行受权且可验证的固件更新，改 `app_trust_anchor.c` 常量"）将锚换为确定性可复验的夹具锚。
- **轮换记录 2（确定性夹具锚 → 受控随机发行者，A REVIEW 裁决，本轮交付）**: 确定性夹具锚（指纹 `396165562e93…`）因私钥可由仓库公开种子+标签复算，被 A 在 PR #65 对 candidate `b5beaa7d` 的 REQUEST_CHANGES 中否决（AC3"设备独立的受控来源"：任何持有仓库代码者可重算私钥并签出被实验固件接受的任意 P2 包）。本轮按同一 §3.3 第 2 条轮换规则换为**一次性真实随机生成**的 P-256 受控发行者（`openssl ecparam -name prime256v1 -genkey`，操作系统 CSPRNG；新指纹 `06e732b1b94d…`），锚结构不变、仅两个 `const` 字节数组替换；同批删除 `anchor_keygen.py` 中全部"公开输入推导私钥"逻辑（种子常量、派生函数、`--export-key-dir`）。旧夹具锚与原未托管随机锚自本固件版本起一并失效。
- **与公开 d=1 测试键隔离**: 公开的 P-256 `d=1` 标量键（SPKI SHA-256 `5cd252fb…`）只作为 `tests/app_admission/` 内明确标识的离线协议夹具信任视图，**不是**设备锚。测试断言设备锚指纹 ≠ d=1 指纹、锚 publisher ≠ `test-publisher`；并验证 d=1 签名的 golden 包与"自称设备 publisher 的 d=1 签名包"在设备锚下均为 `PUBLISHER_UNTRUSTED`。
- 设备锚与 d=1 夹具走**同一段验证代码**（`app_admission_verify_package` 以 `app_admission_trust_view_t` 注入信任视图）；设备路径仅是"视图 = 编译期锚"这一特例，因此黄金/负例向量的判定逻辑对设备锚同样生效。

### 3.1a 受控密钥仪式、保管与配对证明（本轮按 A REVIEW 裁决重构，非生产）

**保管模式选择**：一次性真实随机生成 + 仓库外受控托管。受控发行者密钥在实施环境用 `openssl ecparam -name prime256v1 -genkey`（操作系统 CSPRNG，非确定性）生成一次，SEC1 PEM 保存在仓库之外：

- **托管位置**: `~/.ne301-keys/ne301-app-issuer-v2.sec1.pem`（文件权限 0600，目录 0700），不在仓库、不进任何构建产物或打包镜像；同目录另有 `ne301-fixture-reject-foreign.sec1.pem`（0600）——**仅用于离线拒绝夹具** `ANCHOR_FOREIGN_PACKAGE` 的签名（该夹具 manifest 冒认当前锚指纹、须随每次轮换重签），永不成为设备信任对象；
- **仓库承诺**: 不提交私钥本体，也不提交任何可复现该私钥的算法/种子/标签（前一轮的确定性派生脚本连同种子常量已整体删除）；仓库内只有 SPKI DER/指纹（公钥半）与用它签好的包字节；
- **非生产定位**: 该密钥仅为受控实验 Host 的非生产测试信任锚，不具任何生产发行效力。

`tests/app_admission/anchor_keygen.py`（纯 Python 标准库，无第三方依赖）随之改为**无私钥不可签发**的工具：

1. **默认运行 = 纯验证守卫**：解析 `app_trust_anchor.c` 的 `g_anchor_spki_der[]`/`g_anchor_spki_sha256[]` 与 `anchor_vectors.h` 的向量常量，逐字节比较"编译锚 SPKI == 向量签名者 SPKI"、双向重算比较 SHA-256 指纹、校验正例包 manifest 指纹字段，并用锚公钥独立验签：正例包必须通过、两个负例夹具包必须失败；任何漂移退出码 1。全程只用公开数据，不需要也不接受私钥。
2. **`--emit-header <header> --key <pem>` = 轮换/重签（唯一私钥入口）**：从托管路径加载 SEC1/PKCS8 私钥（脚本不内嵌任何密钥材料或派生逻辑，从不在终端/日志输出标量），以 RFC 6979 deterministic k + low-S 重签正例包；`--foreign-key <pem>`（可选但轮换时必需，见守卫强制）同步重签冒认新锚指纹的外来拒绝夹具；随后重写 `anchor_vectors.h`（逐字保留 superseded 静态夹具数组）并同步替换 `app_trust_anchor.c` 的两个常量数组，最后自动执行第 1 条守卫。**没有托管私钥则无法运行**；省略 `--foreign-key` 时守卫会因夹具指纹过期而退出码 1（fail-closed，不允许静默产出过期负例）。
3. **确定性公开夹具键的处置**：`ANCHOR_SUPERSEDED_PACKAGE`（遗留轮换/未知锚版本夹具键签名+指纹 → `PUBLISHER_UNTRUSTED`）保留为**静态拒绝夹具**（其指纹只指向自身签名键、与设备锚无关，轮换不必重签）；`ANCHOR_FOREIGN_PACKAGE`（manifest 冒认当前锚指纹但为外来键签名 → `SIGNATURE_INVALID`）由托管的外来夹具钥随每次轮换重签。两者明确不受设备信任、从任何"设备受信锚"路径摘除；守卫断言其签名/指纹关系与设备锚互异，不存在以夹具键降级 AC3 的路径。
4. **配对证明（双向、逐字节）**：
   - 脚本侧：第 1 条守卫即"锚↔键漂移的永久检查"，任何不一致退出码 1。
   - 测试侧：`test_app_admission.c` 将**真实固件翻译单元** `app_trust_anchor.c` 链接进宿主测试二进制（与固件同源对象，非复制品），断言编译锚 SPKI/指纹与 `anchor_vectors.h` 中的签名键 SPKI/指纹逐字节相等，并再次重算 SHA-256 比对。
   - 独立交叉验证：本轮已用 OpenSSL（与脚本两套独立实现）从托管密钥导出公钥，`SHA-256(SPKI DER)` 与固件常量三方逐字节一致（`06e732b1…`）。
5. **签名确定性**：ECDSA 采用 RFC 6979 deterministic k（SHA-256）+ low-S 归一——同钥重签字节级相同，`anchor_vectors.h` 可由持有托管私钥的受控环境逐字节再生（已验证两次生成 header sha256 一致：`0f2d49a8…`）。
6. **端到端正例（真实编译锚代码路径）**：
   - `test_app_admission`：以 `app_trust_anchor_get()` 返回的**编译期锚**构造信任视图，`app_admission_verify_package(ANCHOR_POSITIVE_PACKAGE)` 返回 `APP_ADMISSION_OK`（`trusted_compatible`），verdict 提取 publisher=`ne301-exp-pub`、app=`anchor-fixture`、v1.0.0、TBS 长度与键指纹全对。
   - `test_app_admission_gate`：同一锚签包以 base64 POST 进**真实固件 API handler** `app_admission_verify_handler()`（含高权限门 → base64 有界解码 → 锚自检 → 验签 → verdict JSON），返回 `"verdict":"trusted_compatible"` 且 `"executable":false,"installed":false`。
7. **错配拒绝矩阵（全部对真实编译锚）**：

| 用例 | 包构造 | 预期结果 | 断言名 |
| --- | --- | --- | --- |
| 正例 | 锚键 RFC6979 签名，锚指纹入 manifest | `trusted_compatible` | `anchor_positive_trusted_compatible` / gate `gate_anchor_positive_verdict` |
| 轮换/未知锚版本 | 遗留轮换夹具键签名+指纹（同 publisher 名，静态拒绝夹具，不受设备信任） | `PUBLISHER_UNTRUSTED`（指纹先行恒时比较拒绝） | `anchor_superseded_version_refused` / gate `gate_anchor_superseded_refused` |
| 错发行者签名 | manifest 声称锚指纹，但签名来自外来键 | `SIGNATURE_INVALID`（指纹通过后验签拒绝） | `anchor_foreign_signature_refused` |
| 篡改载荷 | 正例包镜像末字节 XOR 0xFF | `SIGNATURE_INVALID`（TBS 覆盖镜像） | `anchor_tampered_payload_refused` |
| 篡改签名 | 正例包 r 整数内翻转 1 bit | `SIGNATURE_INVALID` | `anchor_tampered_signature_refused` |
| 截断签名 | 正例包少 1 字节 | `BAD_PACKAGE`（DER 全消费） | `anchor_truncated_signature_refused` |
| d=1 隔离（回归） | 公开 d=1 键 golden / 冒名 publisher | `PUBLISHER_UNTRUSTED` | `d1_golden_refused_by_device_anchor` / `d1_signature_device_identity_refused` |

8. **边界声明**：以上全部为宿主（macOS）模拟证据，非设备运行证据；托管私钥与夹具密钥**均不得**用于任何生产签名；旧确定性夹具锚（`396165562e93…`）与更早的未托管随机锚（`f5ca101c…`）均视为失效——按 §3.3 规则自本固件版本起旧锚签发的任何包都会被新锚拒绝（无在线吊销语义，仅锚常量替换语义）。

### 3.2 不可替换性

- 锚数据只在 `.rodata`；模块**不提供任何 setter/写接口**；上传包、普通 NVS、LittleFS、Web API 均无任何写入路径（#36 门 + 代码结构双重保证）。
- `app_trust_anchor_status()` 在每次 verify 前自检：版本、publisher 绑定与字符集、SPKI 长度、`SHA-256(SPKI) == 内置指纹`（恒时比较）——任何一项不成立返回 `MISSING`/`MALFORMED`，verify 入口按 `TRUST_UNPROVEN` fail closed，不降级、不跳过。
- SPKI 解析器严格校验（外层 SEQUENCE 完整消费、算法 OID `id-ecPublicKey`、曲线 OID `prime256v1`、BIT STRING 65B 未压缩点、`ecp_check_pubkey`），锚被构建期损坏同样 fail closed。

### 3.3 版本化更新/失效规则（v1 语义，照规范 §4.1 复述为本实现的约定）

1. **v1 单锚**: 一个固件版本只承认一个 `publisher_id → SHA-256(SPKI DER)` 映射；无在线吊销、无双钥过渡、无自动迁移。
2. **更换/撤销** = 另行受权且可验证的 Host/信任策略固件更新（改 `app_trust_anchor.c` 常量 + `format_version` 递增），自新 Host 生效后的下一次安装/执行请求起按新映射拒绝旧钥包。
3. **失效状态可观察**: `GET /api/v1/app/admission/state` 向管理员返回锚 `format_version`、`publisher_id`、`status(provisioned/missing/malformed)`；`status != provisioned` 时 verify 一律 `TRUST_UNPROVEN`。
4. **残余限制（诚实声明）**: 当前 OTA 无验签、无已证硬件可信启动——"编译进固件"只是与 Host 代码同生命周期的相对信任，**不能宣称**抗固件替换、远程安全轮换或硬件信任根；固件可被替换的设备上锚的可信性随之塌陷。本实现不解决也不掩盖这一点。

## 4. 管理准入与入口控制（AC2）

- **路由**: `POST /api/v1/app/admission/verify`、`GET /api/v1/app/admission/state`，路由表 `require_auth=AICAM_TRUE`，且 handler 内第一步再次调用 `web_server_high_privilege_allowed(ctx)`（与 `system_factory_reset_handler` 同型，深防御）；公开默认口令在 #36 判定下永不授予高权限（该性质由 #36 基线 1960 checks 钉死，本套件另测门函数本体对 NULL/仅认证/仅高权限组合的拒绝）。
- **先授权后解析**: 403 分支不读取、不解析 body，未授权者得不到任何包/密钥/发行者信息。
- **验证 ≠ 可执行**: verify 仅返回 verdict JSON；响应恒含 `"executable":false,"installed":false`（含 trusted_compatible 情形）；代码中不存在 verify → 执行/安装的任何调用边（#30/#31 将来按各自授权消费本结果，且必须重新核实实际字节与当前信任身份）。
- **实验 UART 装载默认关闭**: `app_host_load_and_run()` 顶部新增 arm 门（`g_experiment_armed` 默认 `AICAM_FALSE`，未 arm 返回拒绝）；仅 CLI `apphost arm` 可在**当前 boot** 内 arm（断电复位即恢复默认关闭），`apphost disarm`/`info` 可观察；Web 无任何 arm 入口。整个 AppHost/准入栈只在 `APP_HOST_POC=1`（且 64MiB PSRAM）构建存在——默认生产构建无这些符号（ELF nm 证据）。
- **输入有界**: base64 解码前先做长度上限检查（协议上限导出），拒绝超大输入再分配；解码失败/空输入拒绝；malloc 失败显式报错，不虚报成功。

## 5. 验证证据与边界（AC4）

### 5.1 host 静态/模拟（本轮实际执行）

| 套件 | 命令 | 结果 |
| --- | --- | --- |
| 新: 协议核心 | `cd tests/app_admission && make test`（`test_app_admission`） | **179 checks, 0 failures** |
| 新: API 门 | 同上（`test_app_admission_gate`） | **85 checks, 0 failures** |
| 基线: storage gate | `cd tests/storage_gate && make test` | 802/0, e2e 267/0, admin auth 1960/0, auth boundary 83/0 |
| 基线: app_host | `cd tests/app_host && make test` | 21 checks, 0 failures |
| 基线: sign spike | `cd tests/sign_spike && make` | 22/22 |
| 锚绑定守卫 | `python3 tests/app_admission/anchor_keygen.py` | 锚↔向量↔签名绑定全绿，退出码 0；纯公开数据，无私钥可运行 |
| 锚轮换/重签（需托管钥） | `python3 tests/app_admission/anchor_keygen.py --emit-header tests/app_admission/anchor_vectors.h --key ~/.ne301-keys/ne301-app-issuer-v2.sec1.pem --foreign-key ~/.ne301-keys/ne301-fixture-reject-foreign.sec1.pem` | RFC6979 重签字节级确定，重复运行 header sha256 相同（`0f2d49a8…`），退出码 0 |
| docs-sync | `python3 Script/gen_web_api_docs.py --check` | 提交后一致 |

新协议套件覆盖：标识符字符集 14 例；golden 全字段正例；§2.5 全部固定正/负向量（含 low-S、70B DER、FF 尾、reserved0 已签名负例与 7 个固定已签名负例）；自写 DER 解析器负例（s=0、s≥n、负数编码、非最短 SEQ 长、BER 不定长、截断）；结构负例（magic/版本/保留位/两个长度/溢出/整包上限/签名截断）；manifest 编码负例（大写、内嵌 NUL、非零填充、数字开头、reserved、未知 caps 位）；原生头 v1 严格化负例（header_size=33、format=2、magic、reserved0、CRC、entry 越界/奇对齐、manifest↔头不一致、摘要不符）；策略矩阵（PSRAM 32、错板、ABI、caps 不支持、执行区基址、装载/驻留容量三联）；信任失效（缺钥/空钥/无 publisher 绑定 → `TRUST_UNPROVEN`）；设备锚性质与 d=1 隔离负例；**编译锚配对与正/错配证据 21 例**（3.1a 节矩阵：SPKI/指纹逐字节配对、重算摘要、锚键签名的端到端 `trusted_compatible` 正例、轮换键/错签名/篡改载荷/篡改签名/截断签名拒绝）。
门套件新增 **15 例**：锚签包经真实 `app_admission_verify_handler`（高权限门→有界解码→锚自检→验签→verdict JSON）端到端返回 `trusted_compatible` 且 `executable:false`/`installed:false`，及未授权 403 与轮换键包同通道拒绝。
mbedTLS 库部分按 `tests/sign_spike` 同一拓扑（固件同源 108 TU，host 关 ALT 软件路径）编译；这是 **macOS host 模拟**，非设备执行。

### 5.2 目标固件（交叉编译证据，非运行时证据）

- `rm -rf Appli/build && make app -j16`（默认 `APP_HOST_POC=0`）→ 完成；`nm` 确认 `app_admission_*`/`app_trust_anchor_*` 符号不存在。
- `rm -rf Appli/build && make app APP_HOST_POC=1 -j16` → 完成；text 3,472,712B（默认 3,460,624B，准入栈含被引用的 mbedTLS 验签路径约 +12KB）；`nm` 确认 `app_admission_verify_package`/`app_trust_anchor_status`/两个 API handler 及 `mbedtls_ecdsa_verify`、`mbedtls_ecp_point_read_binary` 在 ELF 中留存（关闭 #32 遗留的"链接器是否剔除"缺口）。
- 工具链: Arm GNU 15.2.Rel1（`arm-none-eabi-gcc 15.2.1`）。
- **未做**: 任何真机烧写/串口/PKA 对照/运行时验证。设备 PKA ALT 与软件路径的一致性、设备 ROM/RAM 实测占用、真机拒绝行为仍属后续受控设备授权范围，本轮不作任何声明。

### 5.3 能力边界声明

签名验证通过**不**意味着：App 已安装/可持久执行、原生代码有沙箱或故障隔离、管理员通道机密性已证明、设备具备防固件替换能力。已验证 App 仍是特权原生代码（规范 §4.2）。`TARGET_INCOMPATIBLE` 等判定使用**构建期常量策略**（board `0x00003010`、PSRAM 64、ABI `0x00010000`、caps `log|tick`、执行区基址/大小来自 `app_host_exec_region()` 链接符号），不等于对真实板卡容量/身份的运行时探测。

## 6. 复现指引

```sh
cd tests/app_admission && make test     # 179 + 85 checks，退出码即结果
python3 anchor_keygen.py                # 锚↔向量↔签名绑定守卫，任何漂移退出码 1
cd ../storage_gate && make test         # 802/267/1960/83
cd ../app_host && make test             # 21
cd ../sign_spike && make                # 22
rm -rf Appli/build && make app -j16                # 默认镜像（无准入代码）
rm -rf Appli/build && make app APP_HOST_POC=1 -j16 # 实验 PoC 镜像（含准入）
python3 Script/gen_web_api_docs.py --check         # 提交后 OK
```

## 7. 遗留与升级建议

1. ~~设备锚私钥当前仅存在于实施环境且未持久托管~~（已按 A REVIEW 裁决最终解决：现行锚为一次性真实随机生成的受控发行者，SEC1 私钥托管于 `~/.ne301-keys/ne301-app-issuer-v2.sec1.pem`（0600，仓库外），仓库内无任何复现算法/种子/标签；确定性夹具锚已轮换出局，记录见 §3.1a/§3.3。#30/#40 需持续签发测试包时，在同一受控环境以 `anchor_keygen.py --emit-header <header> --key <托管PEM>` 重签——仍非生产）。
2. `docs/web-api/endpoints/app_admission.md`（en/zh）由 `Script/gen_web_api_docs.py` 自动生成，模块标题回退为键名（生成器 `MODULE_META` 未加条目——`Script/**` 不在本任务 scope）；A 可在后续任务中补充元数据。
3. HTTP 状态码/JSON 字段最终映射待 P5 冻结；当前映射（200+verdict / 403 / 400）是本实现选择，不预设规范结论。
4. `P2 §2.5` 的"`golden+1B`"样本未公布追加字节值，套件以自造 `0xFF` 尾替代（同语义：73B 签名段越上限拒绝），其余向量均逐字节复现。
