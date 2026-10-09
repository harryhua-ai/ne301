# App 数字签名验签可行性调查（Issue #32 Spike：SHA-256 + ECDSA P-256）

- Issue: harryhua-ai/ne301#32（以 issue 内 `ght-contract` 为准）
- 调查基线: worktree `agent/32/c0aa70ba`，HEAD `b00584d5`（= `experiment/app-host-poc` 经 PR #28 合入后的实验分支 HEAD，含 #25/#27 的 App Host PoC 代码与 `Docs/design/app-host-poc.md`、`Docs/design/app-install-feasibility.md` 两份前序文档）
- 本会话性质: **纯离线调查 + host 端编译/测试实证**。无设备操作（无刷写/串口/OTA），无生产密钥，无安装器/签名平台产出
- 定位: **调查证据文档，不是规范，不是 P2 决策权威**。算法/协议/信任模型取舍归 A（权威产品决策在 ne30x-app#4；协议草案在 ne30x-app Draft PR #5，**未冻结**）
- 证据等级标注:
  - 【构建实证】= 本会话在 macOS 上用固件同源源码/配置实际编译、链接、运行得到的命令与输出，可按 §5 指引复现
  - 【源码/静态分析】= 给出 `file:line` 锚点，读者可对照基线 commit 核对
  - 【推断】= 从源码语义/构建系统行为推导，未经设备证实
  - 【尚需验证】= 当前不可证实，列入 §6 清单

---

## 0. 结论速览

| 问题 | 结论 | 证据 |
|---|---|---|
| 固件实际配置是否启用 ECDSA P-256 + SHA-256 验签所需模块？ | **是**。固件解析配置（`config-ccm-psk-tls1_2.h` + morselib 尾 include）含全部所需开关，且配置自身（非本会话追加）连 `ASN1_WRITE_C`/`PK_WRITE_C` 都已启用 | 【源码/静态分析】§1.2，【构建实证】§1.4 |
| 同源源码能否在离线环境实际编译、链接、跑通 sign/verify？ | **能**。108 个 `library/*.c` 全量编译零错误，链接测试程序后 **22/22 检查通过**（含四组 AC2 必备用例 + 非规范/截断 DER 用例 + openssl 互操作） | 【构建实证】§1.4/§2 |
| wire-format 是否已定？ | **没有**。本 Spike 只证明算法可用性与标准 DER/SPKI 互操作；.neapp 信封（签名覆盖范围、哈希输入构造、密钥编码）在 ne30x-app Draft PR #5，未冻结 | 【推断】§4 |
| 信任锚现状？ | **设备不存在任何代码签名信任锚**（无内嵌发行方公钥、NVS/LittleFS 存钥无防篡改、OTA 无验签、管理凭据弱）。固件随版公钥是唯一具备"随受保护分发渠道同生命周期"性质的候选，但其强度受制于 OTA 链路本身无验签 | 【源码/静态分析】§3 |
| 给 A 的一句话 | **算法层可行已实证（P-256 钉死为唯一设备可用曲线），但 P2 冻结前必须把"信任锚供给/轮换机制"与"签名信封"分开决策；当前任何方案都不产生硬件级安全根** | §3.4/§6 |

三个概念严格区分（AC2 要求）：

1. **算法可用性** = mbedTLS 配置/源码层能做 P-256 + SHA-256 验签 —— 本文档已实证；
2. **.neapp wire-format** = 签名覆盖哪些字节、如何编码 —— 未冻结，本文所有 sign/verify 均只针对固定 64 字节测试串与标准 DER 签名，不预设信封；
3. **生产信任方案** = 公钥从哪来、如何防篡改、如何轮换 —— 仅做可行性对比（§3），不构成决策。

---

## 1. AC1：库配置与可编译/可链接性证据

### 1.1 库位置与版本

【源码/静态分析】固件树内唯一 mbedTLS 位于 `Custom/Common/Lib/MbedTLS/`，布局为 `configs/ include/ library/ port/` 四目录。版本 **3.6.4**：`include/mbedtls/build_info.h:36-38`（`MBEDTLS_VERSION_NUMBER 0x03060400`，`MBEDTLS_VERSION_STRING "3.6.4"`）。

【源码/静态分析】固件构建接线（`Appli/Makefile`）：

- `:296-297` — `C_SOURCES += $(wildcard ../Custom/Common/Lib/MbedTLS/library/*.c)` **和** `port/*.c`：**全量**编译两个目录；
- `:648` — `-DMBEDTLS_CONFIG_FILE="config-ccm-psk-tls1_2.h"`：固件实际生效的配置文件；
- `:808-811` — include 路径 = `configs/ include/ library/ port/` 四处。

### 1.2 配置开关核对（固件实际解析的配置）

【源码/静态分析】`Custom/Common/Lib/MbedTLS/configs/config-ccm-psk-tls1_2.h`（下称"主配置"）：

| 开关 | 行号 | 状态 | 与验签的关系 |
|---|---|---|---|
| `MBEDTLS_SHA256_C` | 50 | ✅ 启用 | 摘要 |
| `MBEDTLS_SHA256_ALT` / `MBEDTLS_HAL_SHA256_ALT` | 51-52 | ✅ 启用 | 摘要走硬件 HASH 外设（`port/sha256_alt.c`） |
| `MBEDTLS_ASN1_PARSE_C` | 104 | ✅ 启用 | DER 签名解析 |
| `MBEDTLS_BIGNUM_C` | 105 | ✅ 启用 | 大数运算 |
| `MBEDTLS_PK_C` / `MBEDTLS_PK_PARSE_C` | 107-108 | ✅ 启用 | 密钥抽象/公钥解析 |
| `MBEDTLS_PEM_PARSE_C` / `MBEDTLS_BASE64_C` | 109-110 | ✅ 启用 | PEM 公钥载入 |
| `MBEDTLS_RSA_C`（+ `RSA_ALT` 112-113） | 111 | ✅ 启用 | 与 ECDSA 无关，但影响配置解析 |
| `MBEDTLS_ECP_C` | 132 | ✅ 启用 | 曲线算术 |
| `MBEDTLS_ECP_DP_SECP256R1_ENABLED` | 133 | ✅ 启用 | **P-256** |
| `MBEDTLS_ECP_DP_SECP384R1_ENABLED` | 134 | ✅ 启用 | P-384（**但见 §3.5 ALT 限制**） |
| `MBEDTLS_ECDH_C` / `MBEDTLS_ECDSA_C` | 135-136 | ✅ 启用 | 密钥协商/签名 |
| PKA ALT 钩子 `MBEDTLS_ECP_INTERNAL_ALT`、`ECP_NORMALIZE_JAC_ALT`、`ECP_DOUBLE_JAC_ALT`、`ECP_ADD_MIXED_ALT`、`ECDH_GEN_PUBLIC_ALT`、`ECDH_COMPUTE_SHARED_ALT`、`MBEDTLS_ECDSA_SIGN_ALT`、`MBEDTLS_ECDSA_VERIFY_ALT` | 139-146 | ✅ 启用 | **设备端 ECDSA sign/verify 全部走 STM32N6 PKA 硬件**（`port/ecdsa_alt.c`、`port/ecp_alt.c`、`port/pka_p256_alt.c`） |
| `MBEDTLS_PLATFORM_C` + `PLATFORM_MEMORY`，calloc/free 映射到 `hal_mem_calloc_large`/`hal_mem_free` | 127-130 | ✅ 启用 | 内存来源（`#include "mem.h"` 于 :28，实际解析到 `Custom/Hal/mem.h`） |
| `MBEDTLS_USE_PSA_CRYPTO` / `MBEDTLS_PSA_CRYPTO_C` | 91-92 | ❌ 注释禁用 | 传统 pk/ecdsa 路径生效，无 PSA 干扰 |
| `MBEDTLS_SSL_*`/PSK/X509/RSA 等 TLS 栈 | 47-59, 101-103, 117-121 | ✅ 启用 | 与本 Spike 无关但构成配置整体 |

【源码/静态分析】**主配置尾 include（:153-156）**：`#include "../../mmx108/morselib/include/mm_mbedtls_config.h"`（相对 configs 目录解析到 `Custom/Common/Lib/mmx108/morselib/include/mm_mbedtls_config.h`，Morse Micro HaLow WiFi 库的 mbedTLS 需求追加，仓库另有 `mmx108_2_13_1/` 同内容副本，两文件 diff 为空）。该文件为固件解析配置**额外无条件启用**：

- `MBEDTLS_ASN1_WRITE_C`（:46-47）、`MBEDTLS_PK_WRITE_C`（:98-99）、`MBEDTLS_PEM_WRITE_C`（CONFIG_DPP 块内）、`MBEDTLS_PKCS5_C`（:102-103）、`MBEDTLS_CMAC_C`（:58-59）、`MBEDTLS_NIST_KW_C`；
- 曲线追加 `SECP521R1`（:33-34），摘要追加 `SHA224/384/512`（:107-123）；
- CONFIG_DPP 块（主配置 :153-155 定义 `CONFIG_DPP`）再追加 X509 CREATE/CSR/CRL、`ECDSA_C` 等幂等定义。

**要点（对 P2 有直接影响）**：固件最终解析配置里，ECDSA P-256 验签所需模块（SHA-256、ASN.1 parse/write、bignum、ECP、PK、PK parse/write、PEM）**全部处于启用态**——不是"有头文件没配置"，而是配置本身已把签名+验签全栈打开（动机是 WiFi 库 DPP/证书需求，不是为 App 验签；但能力事实成立）。

### 1.3 设备端验签实际执行路径（软件栈 vs 硬件 ALT）

【源码/静态分析】固件同时编译 `library/ecdsa.c` 与 `port/ecdsa_alt.c`，由宏决定谁提供符号：

- `library/ecdsa.c:483-604` — 通用软件 `mbedtls_ecdsa_verify`，仅在 `!defined(MBEDTLS_ECDSA_VERIFY_ALT)` 时编译（:198 守卫）；
- `port/ecdsa_alt.c:35-46` — ALT 版 `mbedtls_ecdsa_verify`：**仅当 `grp->id == MBEDTLS_ECP_DP_SECP256R1` 才转发 `pka_p256_ecdsa_verify`（`port/pka_p256_alt.c:506`，STM32N6 PKA 硬件），其它曲线一律返回 `MBEDTLS_ERR_ECP_BAD_INPUT_DATA`**。

⟹ **固件上可用的 ECDSA verify 只服务 P-256**。配置虽启用 P-384/P-521（主配置 :134、mm 配置 :29-34），但其软件实现在固件里被 ALT 替换、PKA ALT 又不支持这些曲线——若 P2 信封选了非 P-256 曲线，设备将无法验签。**wire-format 应钉死 P-256**。

⟹ 本 Spike 的 host 构建（§1.4）undef 全部 ALT 钩子，走 `library/` 软件实现；软件实现与硬件 ALT 的**数值结果一致性**未在设备对照【尚需验证，§6.4】。PKA ALT 实现自身以 P-256 专用硬件为前提，其正确性由出货 WiFi TLS 功能隐式背书【推断】。

### 1.4 Host 端编译/链接/运行实证【构建实证】

环境：macOS 15.6（arm64），Apple clang 17.0.0，mbedTLS 源码与配置**直接取自本仓库**（无任何第三方拷贝）。

测试资产（全部位于 `tests/sign_spike/`，均为本会话新建）：

| 文件 | 作用 |
|---|---|
| `host_user_config.h` | 先原样 include 固件主配置（含 morselib 尾 include），再**仅**做三类已注释的 host 适配：① undef 18 个硬件 ALT 钩子（§1.3）；② undef 平台内存宏（回退 libc）；③ undef THREADING（单线程测试）。**模块集合与固件解析配置完全一致** |
| `shim/mem.h` | 主配置 :28 `#include "mem.h"` 的 host 解析替身（固件侧解析到 `Custom/Hal/mem.h`）。仿照既有先例 `tests/app_host/lfstool/shim/Hal/mem.h` |
| `Makefile` | 与固件相同拓扑：`$(wildcard library/*.c)` **108 个翻译单元全量编译**；`port/*.c` 不编译（STM32N6 专用）。`make test` 一键编译+运行并存档输出 |
| `test_sign_spike.c` | AC2 测试程序（§2），`MBEDTLS_ALLOW_PRIVATE_ACCESS` + `-DMBEDTLS_CONFIG_FILE='"host_user_config.h"'` |
| `vectors/` + `gen_vectors.sh` | openssl 3.6.2 生成的**非生产**测试密钥/消息/签名向量（§2.2） |

编译证据（命令与结果）：

```sh
cd tests/sign_spike && make
# 每个翻译单元的编译命令形如：
# cc -std=c11 -O2 -g -Wall -Wextra -Ishim -I. \
#    -I../../Custom/Common/Lib/MbedTLS/configs \
#    -I../../Custom/Common/Lib/MbedTLS/include \
#    -I../../Custom/Common/Lib/MbedTLS/library \
#    -DMBEDTLS_CONFIG_FILE='"host_user_config.h"' \
#    -DMBEDTLS_ALLOW_PRIVATE_ACCESS \
#    -c ../../Custom/Common/Lib/MbedTLS/library/ecdsa.c -o build/ecdsa.o
```

结果：**108/108 个 library 对象零错误编译**（初始仅有的 4 条 `-Wcomment` 警告由注释文本含 `/*` 字样引起，已修正）；与测试程序链接成功；运行输出存档于 `tests/sign_spike/run-output.txt`，运行期自证关键宏（`MBEDTLS_ECDSA_C=1`、`ECP_DP_SECP256R1_ENABLED=1`、`SHA256_C=1`、`ASN1_PARSE_C=1`、`BIGNUM_C=1`、`PK_C=1`、`PK_PARSE_C=1`、`PK_WRITE_C=1`、`ASN1_WRITE_C=1`、`USE_PSA_CRYPTO=0`、三个 ALT 全为 0）。

足迹参考【推断，非设备数值】：host `-O2` 下验签路径核心对象 `__TEXT` 合计约 **63 KB**（ecdsa 2.2K + ecp 13.7K + ecp_curves 10.0K + bignum 16.1K + bignum_core 6.5K + sha256 4.9K + asn1parse 3.3K + pkparse 4.0K + pk 1.9K，`make size` 可复现）；108 对象全量 `__TEXT` 约 296 KB。x86/arm 编译器差异、`--gc-sections`、PSRAM/flash 布局均未计入——**设备端占用必须以未来接入调用点后的固件符号表为准**【尚需验证，§6.3】。

一个诚实的边界：固件构建系统今天就把这些源码编译为对象（wildcard），但**没有任何固件代码引用验签符号**，最终固件 ELF 链接器会把未引用 section 丢弃。所以本节证明的是"**同源码+同配置在真实工具链下可编译、可链接、可测试**"，而不是"当前固件镜像里已经带着这段代码"——后者要等 P3 接入时才成立。

---

## 2. AC2：签名/验签测试证据【构建实证】

### 2.1 用例与结果（22/22 通过，完整输出见 `tests/sign_spike/run-output.txt`）

被签对象：固定 64 字节串 `"NE301 app-signing spike fixed byte string #1 0123456789abcdef###"`（`test_sign_spike.c` 与 `vectors/spike_message.bin` 逐字节一致，测试内自检）。摘要 SHA-256 = `01f620a3…512a304`（与 openssl 独立计算一致）。

| # | 用例 | 预期 | 实测 | 返回码 |
|---|---|---|---|---|
| V1 | 对固定字节串签名 → 同钥验签 | 通过 | ✅ PASS | 0 |
| V2 | **openssl 签名**（独立实现、随机 k）经 `pk_parse_public_key`(SPKI DER) + `pk_verify` 验签；另经低层 `ecdsa_read_signature` 双通道复验 | 通过 | ✅ PASS ×3 | 0 |
| V3 | SHA-256 原语摘要 == openssl 独立计算常量 | 一致 | ✅ PASS | 0 |
| N1 | 消息篡改（末字节翻转 1 bit） | 拒绝 | ✅ PASS | `-0x4E00` ECP_VERIFY_FAILED |
| N2 | 签名 r 尾字节翻转 | 拒绝 | ✅ PASS | `-0x4E00` |
| N3 | 签名 s 尾字节翻转 | 拒绝 | ✅ PASS | `-0x4E00` |
| N4 | 错误公钥（第二把测试钥派生点） | 拒绝 | ✅ PASS | `-0x4E00` |
| N5 | 错误公钥（第二把 SPKI DER 经 PK 层） | 拒绝 | ✅ PASS | `-0x4E00` |
| N6 | 截断 DER（少 1 字节） | 拒绝 | ✅ PASS | ASN1_OUT_OF_DATA |
| N7 | DER 尾部追加 0xFF（长度+1 送验） | 拒绝 | ✅ PASS | ASN1_LENGTH_MISMATCH |
| N8 | r = 0（非法范围） | 拒绝 | ✅ PASS | `-0x4E00` |
| N9 | s = 0xFF…FF（> 群阶 n） | 拒绝 | ✅ PASS | `-0x4E00` |
| N10 | s 编码为负 INTEGER（省 0x00 填充，非规范 DER） | 拒绝 | ✅ PASS | `-0x4E00` |
| N11 | BER 不定长 `30 80 …` | 拒绝 | ✅ PASS | ASN1_INVALID_LENGTH |

超出 AC2 最低要求的部分：V2 的跨实现互操作（openssl 签 → mbedTLS 验，SPKI 公钥载入）证明的是**字节级标准互操作**（未来 PC 端签名工具与设备端验签可分头实现），N6-N11 证明解析器对截断/非规范编码的健壮性。

### 2.2 非生产密钥声明（AC2 硬性要求）

- 本 Spike 使用的全部私钥（`TEST_D1`/`TEST_D2` 常量与 `vectors/*.pem`）是本会话由 `gen_vectors.sh` 在本机 openssl 现场生成的**一次性测试密钥**，文件头、脚本注释、文档三处均标注 NON-PRODUCTION；仅用于本离线测试，无任何生产语义；
- 签名中的 k 由测试内 xorshift128（固定种子）提供——是可复现性手段，**不是安全 RNG**，绝不适用于生产签名；
- 未从任何设备、任何现有固件材料提取密钥；未创建任何"生产公钥"。

### 2.3 与 wire-format 的区分（AC2 硬性要求）

本节全部结论的适用范围 = "对固定字节串做 SHA-256 摘要 + ECDSA P-256 DER 签名验签"。以下问题**未**被本 Spike 触及，均在 ne30x-app Draft PR #5（未冻结）：签名覆盖的镜像字节范围（是否含头、CRC 字段如何处理）、哈希输入构造、公钥在信封中的编码（SPKI/raw 64B/PEM）、多重签名/证书链。**不得把本文 V1-V3/N1-N11 的通过外推为"协议已验证"。**

---

## 3. AC3：信任锚分析——固件内置公钥 vs NVS/LittleFS 存钥

### 3.1 设备现状：不存在代码签名信任锚（承接 #28 调查，新增两个锚点）

【源码/静态分析】前序文档 `Docs/design/app-install-feasibility.md` §3 已确认（本文引用不重复展开）：FSBL→App 纯 copy+jump 无任何校验（`FSBL/Core/Src/boot.c:100-127`）；App OTA 写后 CRC 校验整段被注释（`upgrade_manager.c:256-298`）；OTA 头安全段 416 字节全 reserved、`OTA_SIGNATURE_SIZE` 常量无消费（`ota_header.h:121-122, 28`）；管理员密码为自定义 32bit LCG 非密码学哈希 + NVS 明文存储 + 首次启动明文打印进日志（`auth_mgr.c`）；NVS 初始化失败即擦除重启（`storage.c:859-883`）；LittleFS 挂载失败静默格式化（`storage.c:511-515`）；裸写/裸擦 API 无分区越界保护（`storage.c:530-542, 553`）。

**本会话新增锚点**：

- OTA 验签是"枚举占位、无代码路径"：`OTA_VALIDATION_ERROR_SIGNATURE_INVALID` 仅在 `Custom/Services/OTA/ota_service.h:230` 定义枚举、`Custom/Services/OTA/ota_service.c:191` 定义字符串，**全 `Custom/` 无任何 return 该值的语句**（本会话全仓 grep 验证）。即设备升级校验目前仅 CRC32 + 版本 + 分区容量（`ota_service.c:150-178` 的 `ota_get_validation_result_string` 上下文可见 CRC 占位实现）。
- 出站 TLS 信任锚（webhook CA bundle 等）**不能**类比引申为"设备有存钥先例"：CA bundle 编译期内置 + 用户可经 API 用 `/certs/webhook_ca.pem`（LittleFS）覆盖（`json_config_nvs.c:553-608`），该锚自身无完整性/防篡改保护，且与代码安装授权无关（前序文档 §3.2 已定性）。

### 3.2 候选 A：固件随版公钥（编译期常量）

**来源/构建配置形态**【推断，含先例锚点】：以 C 常量数组（`static const uint8_t PUBKEY[65]` 或 SPKI DER 91 字节，见 §2.1 实测 SPKI=91B）编入固件 `.rodata`，编译期由构建系统注入。仓库内同类先例：webhook CA bundle 编译期内置（`webhook_service.c:42,125`，前序文档锚点）。占用：P-256 公钥最紧凑形态为 raw 64B（非压缩点去 0x04 前缀外的 x||y）或 65B（含 0x04），SPKI DER 91B（`tests/sign_spike/vectors/ecdsa_p256_spike_testkey1_pub.spki.der` 实测字节数）——相对 4MB App 分区可忽略【推断】。

**更新边界与安全边界**（这是 A 必须看清的核心性质）：

1. 公钥随固件分发 ⟹ 轮换/撤销 = 发一次固件升级；而**固件升级通道本身无验签**（§3.1）⟹ 信任锚的真实强度 = "OTA 投递通道 + 管理员会话门"的强度，而非密码学强度。攻击者持管理密码即可推送"换了公钥的固件"，模型整体塌陷为与今天相同。
2. 反向结论同样成立：在 OTA 链路补上"用 FSBL 固化的根公钥验 App 固件"之前，固件随版 App 验签公钥能提供的增量价值 = **把"任何持管理密码者可执行任意原生 App"收窄为"持管理密码者 + 知道发行方私钥者"**。对当前唯一管理面是单密码的产品（§3.1），这是有实际意义的纵深，但不是安全根。
3. 无硬件启动认证证据：FSBL 自身的可信性依赖"ROM authentic boot 验 FSBL"这类 option bytes 配置，源码不可见（前序文档 §3.4 定性为【尚需验证】）；N6 的 OTP/调试口关闭状态也未调查。**本文明确不宣称存在硬件安全根**。

**可行性判定：可行，无固件代码阻塞**——所需全部库模块已启用（§1.2）、host 实证可编译可测试（§1.4）、设备端 PKA ALT 仅支持 P-256（§1.3）恰好钉死曲线选择。剩余工程量 = 信封接入点 + 验签调用 + 设备端 footprint 复测，属 P3/P4，不在本 Spike 授权内。

### 3.3 候选 B：NVS / LittleFS 存钥

| 维度 | 锚点 | 判定 |
|---|---|---|
| NVS factory 32K 作为存钥位 | `storage.h:37-42`；NVS init 失败 → 擦除分区并重启（`storage.c:859-883`）；NVS 是普通 NOR 区域，**非 OTP、非硬件写保护**；裸写 API 可越界破坏（`storage.c:530-542`） | 【源码/静态分析】**不构成受保护信任锚**：能写 Flash 的任何主体（含被装载的原生 App——PSRAM 执行区 RWX、MPU 默认特权全通，前序文档 §1.5）可改钥 |
| NVS user 区 | 同上；且 factory/user 同分区同失败语义 | 同上 |
| LittleFS 文件 | 挂载失败静默格式化（`storage.c:511-515`）；文件可经 Web API 上传覆盖（`api_file_module.c:182-341`，持会话即可）；先例 = webhook CA 可被替换（§3.1） | **弱于 NVS**：多一层"文件卷可整卷替换"的失败/攻击面 |
| 与固件随版的对比 | — | 固件随版公钥至少与 App Host 代码同生共死（改钥必须发固件）；NVS/LittleFS 存钥把"改钥"降级为"写一个文件/一段 NVS"——在无分区级写保护的现状下是**更弱**的信任锚 |

**可行性判定：技术上"能存能读"（NVS/LittleFS 驱动与 mbedtls_pk_parse 均可用），安全上不达标**。除非先有：NVS factory 子区硬件写保护证据（option bytes/PCROP，未见【尚需验证】）+ 裸写 API 封堵 + 与签名验签的绑定的产品决策——否则候选 B 只应作为"测试/调试密钥槽"或"候选 A 的轮换缓存（根仍在固件）"使用【推断】。

### 3.4 组合结论（给 A 的锚定对比）

- **候选 A（固件随版）是当前唯一与"App Host 代码本身"同生命周期的锚**，且工程上零阻塞（本文 AC1/AC2 已证）；其强度上限受 OTA 无验签与管理凭据弱两点压制——这两点不修，A 只是纵深防御之一，不是安全根；
- **候选 B（NVS/LittleFS）在现状下弱于 A**，仅在有分区写保护/防 App 自改机制后才有资格讨论；
- 无论选谁：**发行方私钥的保管、轮换流程、撤销策略**都是产品侧新问题，本 Spike 范围外；
- 无沙箱前提不变（前序文档 §1.5）：签名验签解决"谁发行的"，不解决"发行方 App 的运行时危害"。首版手动单次运行的授权模型（ne30x-app#4）仍应保留。

### 3.5 算法选型附带证据

- **曲线必须 P-256**：§1.3 ALT 限制（硬件路径只认 P-256）+ P-256 本身性能/占用最优；
- **签名编码建议 DER（标准 `ECDSA-Sig-Value`）**：mbedTLS `read_signature` 原生入口即 DER（N6-N11 的拒绝行为即该解析器），若另定义 raw r||s(64B) 编码需自写装配层并放弃 N6-N11 的既有健壮性【推断】；
- **公钥编码**：SPKI DER(91B) 与 raw 64B 均可被现有 `PK_PARSE_C` 路径消费（SPKI 直接 parse；raw 需 `mbedtls_ecp_point_read_binary` 装配，两者均在本仓库已启用模块内）【源码/静态分析】；
- 哈希 SHA-256 无争议（已启用且与 openssl 互验一致，§2.1 V3）。

---

## 4. Scope 与非目标遵守声明

- 本会话只新增/修改了 `Docs/design/**`（本文件）与 `tests/sign_spike/**`（测试程序、非生产向量、构建脚本、运行输出存档）；未触碰 `FSBL/Appli/Custom/Web/Frontend/Model/WakeCore/Makefile`，未修改既有 app_host 代码与两份前序文档；
- 无设备操作、无刷写/串口/OTA；无生产密钥创建/提取；未创建签名平台、安装器或 .neapp 生成器；未变更原生 Host ABI；
- 未运行任何 ght 命令、未 push、未建 PR、未触碰 main/counting。

## 5. 复现指引（离线，无设备）

```sh
# 基线：agent/32/c0aa70ba（含本文提交）
cd tests/sign_spike
make          # 编译 108 个固件同源 mbedTLS 对象 + 运行 22 项检查，输出存档 run-output.txt
make size     # 打印证签路径对象大小（host 参考值）
OPENSSL=openssl ./gen_vectors.sh   # 可选：重新生成非生产向量（会改变 TEST_D1/D2，需同步改测试常量）
```

依赖：POSIX make + C11 编译器 + python3（gen_vectors 仅此需要 openssl ≥3.x）。不依赖任何设备、网络或生产材料。

## 6. 尚需验证清单（P2 冻结前/后）

1. **设备端真实占用**：验签路径对象进入固件链接后的 ROM/RAM 增量与 PSRAM 执行区余量影响（§1.4 的 host 数值不可替代）；
2. **PKA ALT 与软件实现的一致性/异常行为**：同签名同公钥在设备 ALT 路径与软件路径的验证结果对照；PKA 对边界输入（r=0、s≥n）的设备侧行为（host 已证软件路径拒绝，设备路径未测）；
3. **固件 ELF 符号留存**：未来接入调用点后，确认链接器未剔除所需对象、footprint 复测；
4. **ROM authentic boot / option bytes / 调试口状态**：决定"固件随版公钥"的信任上限（§3.2 第 3 点），需真机只读检查；
5. **NVS factory 区硬件写保护可行性**（option bytes/PCROP）：候选 B 翻盘的唯一途径；
6. **wire-format 全部未决项**：见 ne30x-app Draft PR #5，本文不预设立场；
7. **管理凭据整改**（密码哈希/存储/日志泄漏）是信任模型的前置整改项，归其它任务。
