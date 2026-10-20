# 阶段 0 证据：身份配对与受控重签包（Issue #40）

- claim: `harryhua-ai-20261019T131308-cc64fc5e`
- 分支: `agent/40/c8db4b56`（base `98e61bd5` = `experiment/app-host-poc` HEAD）
- 执行环境: macOS 本地（ZCode 会话），设备操作另见 02/05 号证据；本阶段无任何设备写入

## 1. 信任锚配对（写前证据门第 ③ 项的基础）

Host 固件编译期锚（本分支原样，未改动，权威源 `Custom/Services/AppHost/app_trust_anchor.c`）：

- publisher_id = `ne301-exp-pub`
- SPKI SHA-256 = `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`
- SPKI DER = 91 字节 P-256 SubjectPublicKeyInfo（十六进制常量内嵌固件，属公开数据）

受控签发私钥保存在仓外受限存储；路径与密钥材料均不写入公开证据。
配对验证命令与结果（本会话实测，未输出任何私钥字节）：

```
$ openssl pkey -in "$SIGNING_KEY_PATH" -pubout -outform DER | shasum -a 256
06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda  -
```

结论：受控签发钥与 Host 锚**逐字节配对**。重签不更换 Host 信任根（锚字节保持不变），也
不构成 A 所禁的"重签发行身份"——所用身份正是锚定义的那个受控身份；历史 App #6 的
`dev-publisher`（SPKI `bc5ae9a7…`）与当前锚不互认，故按任务书以受控钥重签配对包。

## 2. hello-app V1/V2 镜像来源（App #6 受审字节）

- 源仓: `ne30x-app`，分支 `agent/6/9a4daa9a`，HEAD `79d8534e7223e99a392f57905091d7a2b05bdeff`
  （P3 受审态，worktree 干净）
- 构建: `apps/hello-app` Makefile（arm-none-eabi-gcc 15.2.Rel1），ABI 头经
  `tools/fetch_abi_header.sh` 锁定 `ne301@ a5b4bf3…/Custom/Common/Inc/app_host_abi.h`
  （sha256 `9337f684…df23f4` 校验通过）
- V1 native image sha256 = `d6eec431c6c17b84770263ce8e8a9d386f9e798d14024f4452f090c87c6e2d03`
  —— 与 App #6 提交记录宣称的 Issue #2 V1 记录**逐字节一致**
- V2 native image sha256 = `e8526a1ab0500a49b23d09fabf2f3664823b55de99bef301fe816d39d5041bfd`
  —— 与 App #6 注册的实验件记录一致
- 两镜像均 608 B（32B 头 + 576B payload），ABI `0x00010000`，目标 `0x93E00000`，V1/V2 payload 不同

## 3. 受控签发包（P2 v1 冻结协议，仓内可提交字节）

打包器: `ne30x-app package/neapp_pack.py`（P2 v1，RFC-6979 确定性 ECDSA P-256、strict-DER、
打包后自验签 fail-closed）。manifest 声明: publisher `ne301-exp-pub`、app `hello-app`、
board `0x00003010`、PSRAM 64 MiB、ABI `0x00010000`、caps `log|tick_ms`、exec base `0x93E00000`。

| 包 | SHA-256 | TBS(SHA-256) | 版本 |
|---|---|---|---|
| `tests/hello_e2e_40/packages/hello-app_v1.0.0.neapp` | `1c13cac2d01f47cff90d4902eb397f6a62dbd5b6dd9faf43f709ca421153fe56` | `405b299e1636933cc592593901c947a9c368fc46bdeacccbbb46a6dc23107ee1` | 1.0.0 |
| `tests/hello_e2e_40/packages/hello-app_v2.0.0.neapp` | `6d00fba27207fbafa1bae11e854092a918a80bf7a06fe469a4ef7590671b9d15` | `9ceb46d10b9868b9938fa3ee4ba10cdf297d5a4437181d65d4405582c605543d` | 2.0.0 |

每包附 `*.provenance.json`（打包器产出的确定性溯源：package/TBS/image SHA-256、manifest
字段、signer SPKI SHA-256、source_commit）。**证明文件不含也不引用任何私钥字节**。

## 4. AC2 负例向量（`tests/hello_e2e_40/packages/`，同目录生成脚本可复现）

| 向量 | SHA-256 | 期望拒绝类 |
|---|---|---|
| `hello-app_v1.0.0_badsig.neapp`（V1 DER 尾字节翻转） | `0eef90a01528682089f10f2d3e52f376fe44ef3575138a81b1bb799902b77c8f` | SIGNATURE_INVALID |
| `hello-app_v9.9.9_foreign.neapp`（仓外 fixture-reject-foreign 钥签发，manifest SPKI≠锚） | `a6055c5a52311204d91a4e90cf27f0012ab3b384c94ddbbea5fc67baf1d6dd24` | PUBLISHER_UNTRUSTED |
| `hello-app_v1.1.0_badboard.neapp`（board 0x00003110） | `eaf562f93e22cb4072096ef045d8bd59c1e439630aa2b8852a28ac2131aa1e5d` | TARGET_INCOMPATIBLE |
| `hello-app_v1.2.0_badabi.neapp`（manifest+镜像头 ABI 0x00010001） | `cc794ceaa83db6070ce1776e7d656bb0394302715a367a0462bb7f1b257d46a5` | ABI_INCOMPATIBLE |
| `hello-app_v2.0.0_altcontent.neapp`（V1 镜像 + 2.0.0 版本号，合法签名） | `1edb99d8e92522fcfb2134fad859eced581e79ed7c483b480d0fd90dddc787b1` | 准入通过 → 安装器 VERSION_CONFLICT |

## 5. 离线验签套件（对齐 #102/#55 既有验签器语义）

1. `tests/hello_e2e_40/verify_offline_40.py`：纯 Python 逐包验证（结构/manifest/签名/锚配对/
   负例分类/provenance 一致性），锚公钥从固件公开 SPKI 常量导出（SHA-256 与锚常量互证）。
   本会话结果：**34 checks, 0 failures**。
2. `tests/hello_e2e_40/test_hello_e2e_40.c`（`make test`）：直接编译**设备同源验签实现**
   `Custom/Services/AppHost/app_admission.c + app_trust_anchor.c`（逐字节锚断言
   `06e732b1…`），以设备策略（board 0x3010/64MiB/ABI 0x10000/2MB@0x93E00000）验证
   上述真包，并用注入 IO 后端复跑安装器语义（V1 安装→V2 升级→同版冲突→降级拒绝→
   失信钥零写入→运行中 BUSY→卸载）。本会话结果：**55 checks, 0 failures**。

## 6. 密钥纪律声明

- 受控签发私钥与 fixture 拒签钥均保存在仓外受限存储；本会话未提交、未打印、
  未复制、未写入任何证据或日志（除一次会话内命令误带出的终端头部片段，未进入任何仓库
  产物/证据文件/对外报告）。
- 所有公开证据仅含 SPKI SHA-256 指纹与验签结果。
