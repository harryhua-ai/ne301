# Host Sidecar v1 有界签名资产验签与身份拒绝模块（Issue #86）

本记录中的 App 签名与发行者信任链已由 #136 退役。当前 Host 格式和安装策略见 [NEAPP05 无签名单文件格式](./app-unsigned-single-file-format.md)。

- Issue: harryhua-ai/ne301#86（以 issue 内 `ght-contract` 为准）
- Wire 权威: harryhua-ai/ne301#79 §12（NEUI major1 P-256 Sidecar v1 交付字节协议，冻结于 2026-10-15）
- 实现基线: worktree `agent/86/cdac2292`，基点 HEAD `95cc5654`（= `experiment/app-host-poc` 合入 PR #85 后的实验分支 HEAD）
- 受信事实来源（只读）: `Custom/Services/AppHost/app_admission.h`（v3 publisher/key/TBS 语义）、`app_installer.h`（`app_install_record_t` 的 `tbs_sha256`/`publisher_key_sha256` 身份字段）；#29 发行者 SPKI 信任根、#55 v3 准入 canonical DONE
- 定位: **离线 C 安全验证模块 + 负例回归**，供 #83 安装/回滚事务复用。本交付不安装、不激活、不 HTTP 公开 UI 资产、不写 LittleFS/NVS、不启动浏览器/业务进程、不注册进生产 API/安装器/请求路由/Host 资源服务/build/启动链
- 证据等级: 本文档全部结论均为【离线 C harness 实测】；**C harness PASS ≠ 设备 PASS**，真机/生产事实见 §6 NOT_RUN 清单

---

## 1. 交付物

| 文件 | 说明 |
|---|---|
| `Custom/Services/AppHost/app_sidecar_verify.h` | 模块公开接口：常量、结果码、trust/verdict/文件描述类型、流式验证器 |
| `Custom/Services/AppHost/app_sidecar_verify.c` | 实现：有界流式解析 + 严格 DER P-256 验签（MbedTLS 仅用于 ECDSA/ECP 数学运算） |
| `tests/app_sidecar/gen_sidecar_fixtures.py` | 独立 fixture 生成器：按 §12 canonical bytes 自行编制正例 + 50 个负例（Python `cryptography`，测试锚独立于生产信任锚） |
| `tests/app_sidecar/test_app_sidecar.c` | 离线 C harness：155 项断言（正向 + 全负例矩阵 + 信任故障 + API 误用 + 有界性） |
| `tests/app_sidecar/Makefile` | 测试构建入口（不进生产 build） |

新 .c/.h 未注册进 Appli 构建/根 Makefile；模块目前仅由离线 harness 编译链接。

## 2. Wire 格式实现（全部以 #79 §12 为准）

```text
0        8   magic = 4E 45 55 49 01 00 00 00
8        4   manifest_len_u32
12       4   asset_payload_len_u32
16       N   manifest = 128B fixed block + file_count 条有序记录
16+N     A   raw asset payload（record 顺序，无压缩无填充）
16+N+A   8..72  恰好一个严格 DER ECDSA-Sig-Value
```

- **TBS = bytes[0 .. 16+manifest_len+asset_payload_len)**，SHA-256/P-256/ECDSA 签名；`ui_tbs_sha256 = SHA-256(TBS)` 为 Sidecar 内容身份
- manifest fixed block: publisher_id(16, v3 小写标识 NUL 右填充) / app_id(32) / publisher_key_sha256(32) / native_tbs_sha256(32) / version maj.min.patch(u16×3, major≥1) / file_count(u16, 1..64) / host_ui_abi(u32, 必须为 1) / reserved(u32, 必须为 0)
- record = path_len_u16(1..160) + file_len_u32(1..1,048,576) + sha256[32] + path；`manifest_len = 128 + Σ`，≤16,384；`asset_payload_len = Σ file_len`，1..2,097,152
- 路径规则: 相对 POSIX ASCII，段匹配 `[A-Za-z0-9][A-Za-z0-9._-]*`；拒绝 leading/trailing `/`、空段、`\`、`%`、`:`、NUL、`?`/`#`、非 ASCII、绝对路径、重复、ASCII case-fold 碰撞；记录严格升序；必须恰好存在一个 `index.html`
- 整个解析使用溢出安全算术；所有上限为**输入上界**，不是 Host 内存承诺

## 3. 接口说明

### 3.1 信任输入（唯一安全权威，Host 提供）

```c
typedef struct {
    const char *publisher_id;  size_t publisher_id_len;
    const char *app_id;        size_t app_id_len;
    const uint8_t *spki_der;   size_t spki_der_len;
    const uint8_t *native_tbs_sha256;
} app_sidecar_trust_t;
```

- `spki_der` 必须是当前受信 publisher 的 canonical P-256 SPKI（91B 严格形态，离曲线点直接拒绝）；模块自行计算 `SHA-256(spki_der)` 与 manifest 的 `publisher_key_sha256` 比对——**Sidecar 自带/自称的任何公钥一律不采信**
- `native_tbs_sha256` 必须取自已受信 native 安装记录的 `tbs_sha256`（`SHA-256(native TBS)`），**不是** whole-file `package_sha256`——两个键不得混用（`neg_native_pkg_sha_confusion` 负例证明混用即拒）
- 任一字段缺失/SPKI 非法 = 无可信当前安装态 ⇒ `APP_SIDECAR_ERR_NO_TRUST` fail-closed

### 3.2 流式验证器（有界内存）

```c
app_sidecar_result_t app_sidecar_verifier_init(v, trust, file_cb, user);
app_sidecar_result_t app_sidecar_verify_consume(v, data, len);
app_sidecar_result_t app_sidecar_verify_finish(v, out);
app_sidecar_result_t app_sidecar_verify_bundle(data, len, trust, cb, user, out);
```

- 验证器状态体 `app_sidecar_verifier_t` = **17,528 B 固定大小**（harness 断言 ≤20,480 B），由调用方静态/栈分配；模块自身不调用 malloc，**不需要一次分配整个 2MiB bundle**——载荷按任意大小分块喂入（harness 实测 1B/16B/4096B/9973B 分块均通过）
- `file_cb`（可选）只在 `app_sidecar_verify_finish()` 确认当前 native 身份、全部文件 SHA-256 与整包 P-256 签名均通过后回调有界描述（path/file_len/sha256）。`consume()` 阶段不会输出文件描述；格式、身份、资产或签名失败时回调数为零，verdict 不写入
- 错误粘滞：任一步失败后后续 consume/finish 返回同一错误码，不得把错误伪报成功

### 3.3 verdict

```c
typedef struct {
    char publisher_id[17]; char app_id[33];
    uint16_t version_major, version_minor, version_patch;
    uint32_t host_ui_abi; uint16_t file_count;
    uint32_t manifest_len, asset_payload_len;
    uint8_t signature_len;
    uint8_t ui_tbs_sha256[32];
} app_sidecar_verdict_t;
```

- verdict 只含受信 UI 版本、`ui_tbs_sha256` 与有界计数元；**不含** native `package_sha256`/image 哈希等原生安装记录字段，不泄漏原生私密身份
- **UI TBS content ID 与随机 ECDSA DER 明确区分**：同一 TBS 的不同有效 DER（重签）→ 同一 `ui_tbs_sha256`、verdict 全等（`positive_resign_same_tbs` 正例证明幂等；#83 的同 TBS 幂等安装判定可直接以此键为准）

### 3.4 错误码（fail-closed 语义）

`OK / ERR_PARAM / ERR_NO_TRUST / ERR_BAD_FORMAT / ERR_LIMIT / ERR_ABI_INCOMPATIBLE / ERR_IDENTITY_MISMATCH / ERR_ASSET_INVALID / ERR_SIGNATURE_INVALID`（`app_sidecar_result_str` 提供诊断串）。AC2 要求的每类拒绝都有独立负例锚定到具体错误码（见 §4）。

## 4. 测试与证据

### 4.1 确切命令

```bash
python3 Script/version_header.py --fsbl-version 1.0.3.1        # 宿主测试头（gitignored，零仓库变更）
cd tests/app_sidecar && make test                              # 生成 fixture + 编译 + 运行
```

如需 `tests/app_host_session` 回归，先追加 `--stedgeai-version "3.0.0"` 生成 `Custom/Hal/stedgeai_version.h`（同为 gitignored）。

### 4.2 本次 r1 candidate 验证

| 套件 | 结果 |
|---|---|
| `tests/app_sidecar`（新） | `total=155 fail=0` |
| `tests/app_admission` | 179/84/61 checks + gate 97 checks，0 failures（v1/v2/v3 准入回归不退化） |
| `tests/app_installer` | 271/280/165/54 checks，0 failures |
| `tests/app_state` | 188/260 checks，0 failures |
| `tests/app_host_run` | 214 checks，0 failures |
| `tests/app_report` | 552 checks，0 failures |
| `tests/app_host_session` | `total=540 fail=0` |
| ASAN+UBSan 全套件复跑 | r1 未重跑；前次 candidate `8922eb177af3a9e0f6ca951f737cf3228d0ed453` 记录为 `total=129 fail=0` |
| ASAN+UBSan 模糊测试 | r1 未重跑；前次 candidate `8922eb177af3a9e0f6ca951f737cf3228d0ed453` 对 417,036B counting bundle 的 500 组变异均拒绝 |

### 4.3 正向覆盖

- 3 文件小 bundle（逐字节 1B 分块喂入）与 **7 文件 counting 真实文件名结构、417,036B 资产量级** bundle（`assets/index-BqeWX5wD.js` 260,143B / `assets/index-DUrZLGu5.css` 45,942B / `assets/videoWorker-Cfb7ojPI.js` 647B / `assets/websocket-init-yiJ-u6et.js` 2,000B / `index.html` 506B / `libs/FileSaver.min.js` 6,954B / `libs/jmuxer.min.js` 100,844B，文件名与字节数取自 #82 实证；**内容为确定性合成数据，是 mock 资产，不冒充 counting 页面可运行证据**）
- verdict 全字段断言（含 `ui_tbs_sha256` 与 Python `hashlib` 独立计算值比对）；文件回调按序枚举 7 路径 + 长度 + 逐文件 SHA-256
- 高位 r/s 各有真实 P-256 签名正例和负例；负例只移除 DER INTEGER 的 `00` 符号填充并缩短 INTEGER/SEQUENCE 长度，正负 TBS 逐字节相同，正例验签通过、负例以 `ERR_BAD_FORMAT` 拒绝
- 非法签名与错钥重签 bundle 均断言零文件回调、verdict 不写入；合法 bundle 在完整 TBS 与签名输入后、调用 `finish()` 前也保持零回调，验签成功后才交付全部文件描述

### 4.4 负例矩阵（49 例，全部 fail-closed 到预期错误码）

| 类别 | 负例（`tests/app_sidecar/build/neg_*.bin`） |
|---|---|
| 严格 DER | `negative_strict_der_r_msb`、`negative_strict_der_s_msb`（去掉高位 INTEGER 必需的 `00`，TBS 不变→FORMAT） |
| 错签/改签 | `neg_sig_foreign_key`（换钥签同 TBS→SIG）、`neg_sig_lowbit_flip`（改 DER→SIG）、`neg_sig_tag_corrupt`（DER tag→FORMAT） |
| 改载荷/manifest | `neg_payload_flip`、`neg_payload_reorder`（逐文件 SHA→ASSET）、`neg_manifest_mutated`（TBS 失配→SIG） |
| magic/ABI/版本/reserved | `neg_bad_magic`、`neg_bad_major_magic`、`neg_abi_two`、`neg_abi_zero`、`neg_version_major_zero`、`neg_reserved_nonzero` |
| 截断/尾随 | `neg_truncated_last`、`neg_truncated_header`、`neg_trailing_byte` |
| 路径 | traversal/绝对/反斜杠/`..`段/空段/`%`/`:`/NUL/`?`/非 ASCII/前导点/超长/path_len=0/重复/case-fold 碰撞/乱序/缺 index.html（13 例） |
| 超限 | file_count 0/65、file_len 0/>1MiB、manifest_len 0/过小/>16,384、payload 0/>2MiB、Σ 失配（10 例） |
| 身份失信 | 错 publisher/app、自称外钥（key_sha=foreign 且外钥签名）、错 native_tbs（跨原生域重放）、`neg_native_pkg_sha_confusion`（package_sha256 冒充 TBS 键）、签名者外钥（3+2 例） |

### 4.5 fixture 纪律

- 测试锚为独立 P-256 key（seed 派生固定 scalar，`cryptography` 生成），**与生产/设备信任锚（06e732b1… 语义）无任何关系**；`gen_sidecar_fixtures.py` 按 §12 canonical bytes 自行编制并自校验（`ui_tbs_sha256` 由 Python 独立计算并与 C 侧比对）
- `ne30x-app #26` packer 不是本案权威也不是依赖；未做、也未声称任何跨仓 golden 互验

## 5. 设计边界与已知限制

- 模块验证的是**离线工件结构与签名**：PASS 不代表设备 Host 有空闲空间/受信发行者、不代表浏览器 HTTP 交付端到端完整性（#84 域）、不代表真实目标容量
- 版本事务语义（同 TBS 幂等 / 同版本不同 TBS 冲突 / 仅允许同 native 身份升级）属 #83；本模块提供判定所需的全部键（`ui_tbs_sha256`、语义版本三元组、身份四元组一致性）
- "lower Sidecar version 非普通更新"与断电回滚均为 #83 行为，本模块不实现
- 载荷验证为流式单遍，适合 #83 未来接流式源；当前 API 入口为内存分块 feed

## 6. NOT_RUN / UNKNOWN 清单（如实申报）

| 项 | 状态 |
|---|---|
| 真机（STM32N6）上编译/运行验证 | NOT_RUN（本交付无设备操作） |
| 生产 key 签名 bundle 验证 | NOT_RUN（无生产 key，fixture-only） |
| #82 counting UI 真实产物作为包内容验证 | NOT_RUN（仅复用其 7 文件名结构与 417,036B 量级，内容为合成 mock） |
| LittleFS 空闲空间/写入 | UNKNOWN/未涉及（本模块无存储路径） |
| 双版本/断电恢复/回滚 | 未涉及（#83 事务域） |
| 浏览器 HTTP 交付完整性 | 未涉及（#84 域） |
| 与 ne30x-app #26 packer 的跨仓 golden 互验 | NOT_RUN（等待 #26 独立交付后另行集成验收） |
