# NE301 Host 生产发行者信任选择实施总结

## 交付边界

- 基线：`a20d055cff0334ec3a483088dc8d80e9ad451e96`，分支 `release/host-production-spki`。
- `APP_TRUST_PROFILE` 默认 `test`，显式 `production` 只使用编译期注入的单一 issuer；缺失配置为 `unprovisioned`，非法配置为 `malformed`，不回退到 `ne301-exp-pub`。
- 生产 publisher/SPKI/provenance 尚未由管理员或用户指定；生产身份与设备验收保持 BLOCKED。本变更没有生产私钥、secret 或硬件操作。

## H1–H3 结果

| 验收 | 结果 | 证据 |
|---|---|---|
| H1 | PASS（fail-closed 能力） | 13 个 profile 编译矩阵覆盖默认 test、production 未配置、合成有效锚、缺项、非法/空/超长 publisher、test publisher、DER 错、SHA 摘要/格式错、出处缺失/为空；全部通过。 |
| H2 | PASS（synthetic TEST） | 独立随机 P-256 synthetic issuer 的真实 native、NEUI、Pair ECDSA/SHA-256 签名沿用 #124 gate；157 项通过，覆盖 test 包拒绝、混配、畸形 Pair、包内根替换、安装/run、重放、同序冲突与降级。 |
| H3 | PASS（本地 Linux/主机构建） | 三个相关测试套件通过；根 `make app` 默认 test 档位通过；非法 profile 在 Appli Makefile 解析期拒绝。CI 尚待 PR 运行。 |

## 测试与构建

- 基线：`app_admission` 421 checks；`app_installer` 735 checks；`app-platform-offline` 2 validations；全通过。基线 installer 的签名 Pair fixtures 为 `NOT_RUN`。
- 最终 admission：179 + 84 + 61 + 100 + 8 = 432 checks，0 failures；profile matrix 13 cases，0 failures。
- 最终 installer：原有 735 checks，0 failures；synthetic production Pair gate 157 checks，0 failures。
- 最终 offline：2 validations，均通过。
- `make app` 默认构建成功。ELF 编译响应选择 `APP_TRUST_PROFILE_TEST`，产物包含 test provenance 和原 SPKI SHA-256 `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`。
- 基线对照：从 a20d055c 编译旧 anchor 后，`HELLO_V1_PACKAGE` 返回 `APP_ADMISSION_OK`（结果码 0）；修复后的 production synthetic gate 拒绝同一 test issuer 包。

## 范围说明

- 额外改动 `Custom/Services/Web/api/api_app_admission_module.c`：仅为 H3 增加现有只读状态响应的 profile、SPKI SHA-256 和 provenance。
- `.plans/` 保存本任务的计划、决策记录和总结，遵循仓库 `AGENTS.md` 工作流。
- 未修改 NEPK/native/NEUI wire、#124 运行时、App UX、OTA/FSBL/存储/信任根持久化路径。

## 待完成

- 创建 Issue-free PR，等待并记录 `main.yml` 与 `docs.yml` CI。
- 取得并确认指定来源的 production publisher ID、DER SPKI、SHA-256 指纹和出处标识后，方可配置真实生产身份并进入设备验收。
