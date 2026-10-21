# NE301 Host 生产发行者信任选择实施总结

## 交付边界

- 基线：`a20d055cff0334ec3a483088dc8d80e9ad451e96`，分支 `release/host-production-spki`。
- `APP_TRUST_PROFILE` 默认 `test`，显式 `production` 只使用编译期注入的单一 issuer；缺失配置为 `unprovisioned`，非法配置为 `malformed`，不回退到 `ne301-exp-pub`。
- 生产 publisher/SPKI/provenance 尚未由管理员或用户指定；生产身份与设备验收保持 BLOCKED。本变更没有生产私钥、secret 或硬件操作。

## H1–H3 结果

| 验收 | 结果 | 证据 |
|---|---|---|
| H1 | PASS（fail-closed 能力） | 14 个 profile 矩阵案例通过；3 个固件编译负例拒绝 synthetic class、`SYNTHETIC_TEST_ONLY` provenance 和 `ne301-exp-pub`，运行时 test publisher 也被拒绝。覆盖缺项、非法/空/超长 publisher、DER 错、SHA 摘要/格式错及出处/class 错误。 |
| H2 | PASS（synthetic TEST） | 独立随机 P-256 synthetic issuer 的真实 native、NEUI、Pair ECDSA/SHA-256 签名沿用 #124 gate；157 项通过，覆盖 test 包拒绝、混配、畸形 Pair、包内根替换、安装/run、重放、同序冲突与降级。 |
| H3 | PASS（本地 Linux/主机构建） | 三套相关测试通过；默认 `make app` 与 production 无配置 `make app` 均通过。test → production → test 档位切换均重编锚点对象并更新 ELF 身份；production ELF 明确为 `unprovisioned`。Docs sync 本地检查通过。最新 CI 等待推送后重跑。 |

## 测试与构建

- 基线：`app_admission` 421 checks；`app_installer` 735 checks；`app-platform-offline` 2 validations；全通过。基线 installer 的签名 Pair fixtures 为 `NOT_RUN`。
- 最终 admission：179 + 84 + 61 + 100 + 8 = 432 checks，0 failures；profile matrix 14 cases、3 个固件编译拒绝案例，0 failures。
- 最终 installer：原有 735 checks，0 failures；synthetic production Pair gate 157 checks，0 failures。
- 最终 offline：2 validations，均通过。
- `make app` 默认构建成功，production 无配置构建成功且输出 `production/unprovisioned`；test → production → test 连续构建均重编锚点对象。默认 ELF 编译响应选择 `APP_TRUST_PROFILE_TEST`，包含 test provenance 和原 SPKI SHA-256 `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`。
- ARM GNU 15.2.1 真实固件构建对 synthetic class/provenance 与 `ne301-exp-pub` 均在编译期拒绝。`python3 Script/gen_web_api_docs.py --check` 通过。
- 基线对照：从 a20d055c 编译旧 anchor 后，`HELLO_V1_PACKAGE` 返回 `APP_ADMISSION_OK`（结果码 0）；修复后的 production synthetic gate 拒绝同一 test issuer 包。

## 范围说明

- 额外改动 `Custom/Services/Web/api/api_app_admission_module.c`：仅为 H3 增加现有只读状态响应的 profile、SPKI SHA-256 和 provenance。
- 额外改动 `docs/public/web-api/endpoints-manifest.json` 与 `docs/web-api/endpoints/manifest.json`：只更新 admission 模块源哈希，满足 Docs sync CI。
- 独立审查发现的增量构建缓存与 synthetic 固件配置风险已修复，并以档位切换和固件编译负例验证；构建签名使用 SHA-256。审查提出的 admission 状态 JSON 扩展风险经评估后保留：该只读元数据满足 H3，未修改 NEPK/native/NEUI wire 或 Web UX。
- `.plans/` 保存本任务的计划、决策记录和总结，遵循仓库 `AGENTS.md` 工作流。
- 未修改 NEPK/native/NEUI wire、#124 运行时、App UX、OTA/FSBL/存储/信任根持久化路径。

## 待完成

- Issue-free PR #128 已创建；更新后的提交尚待推送，随后需记录 `main.yml` 与 `docs.yml` CI。
- 取得并确认指定来源的 production publisher ID、DER SPKI、SHA-256 指纹和出处标识后，方可配置真实生产身份并进入设备验收。
