# NE301 Host 生产发行者信任选择实施计划

**目标：** 为 Host 提供默认非生产 `test` 与显式 `production` 信任档位；生产档位只接受编译期指定的单一 P-256 SPKI，缺失时关闭信任。

**方案：** `APP_TRUST_PROFILE` 贯通根 Makefile 与 Appli Makefile，默认 `test`。生产档位从外部配置头注入 publisher ID、DER SPKI、SHA-256 十六进制摘要和出处；锚点模块自检标识、DER、摘要与出处并拒绝 `ne301-exp-pub`。测试为每次运行生成独立 synthetic 发行者，构造真实签名输入并复用 #124 gate。

**技术栈：** C11、GNU Make、Python 3、仓库内 MbedTLS、OpenSSL-compatible P-256 测试签名。

**设计说明：** `Docs/design/host-production-trust-selection.md`

## 全局约束

- 不生成或读取生产私钥，不修改 GitHub Secrets/Environment，不刷机、不改 FSBL/OTA/NVS。
- 不改变 NEPK/native/NEUI wire，不复制 #124 状态机，不允许 production 同时信任 test。
- 生产身份未提供时仅交付 `unprovisioned` fail-closed 能力；生产身份与设备门保持 BLOCKED。
- C/H/Makefile/Python 不增加注释；默认构建和 #110 行为保持不变。
- 所有提交只包含本计划列出的 Host trust、构建接线、测试和说明文件。

## 审查重点

- 未设置或部分设置生产头时，生产状态为 `unprovisioned` 且锚点不暴露 test 公钥。
- 错误 publisher、非法 P-256 DER、摘要错误或空出处返回 `malformed`。
- `ne301-exp-pub` 在 production 配置中被拒绝；同一有效 test 包在 test 档位通过、在 production synthetic 档位拒绝。
- publisher/SPKI 混配、错误 Pair、重放、同序冲突、降级和包内根替换均拒绝。
- 合法 synthetic native、NEUI、Pair 三签名沿用 #124 安装、session/run 与 floor gate。

## 实施任务

### 任务 1：锚点档位接口与自洽校验

**文件：** `Custom/Services/AppHost/app_trust_anchor.h`、`app_trust_anchor.c`；新增 `tests/app_admission/test_app_trust_profile.c` 与 profile 测试驱动。

- [x] 先写 profile 状态测试：test 身份和摘要兼容；production 无配置为 `unprovisioned`；合法 synthetic 配置为 `provisioned`；缺项、digest 错、DER 错、publisher 非法及 `ne301-exp-pub` 为 `unprovisioned` 或 `malformed`。
- [x] 运行新增测试，确认缺失接口或行为导致预期失败。
- [x] 实现档位、provenance 与 SHA-256 识别接口；production 只构造注入锚，不回退到 test 常量；验证 P-256 SPKI DER、publisher、digest、出处和出处类别。
- [x] 运行 profile 测试，并运行 `tests/app_admission`。

### 任务 2：构建选择和现有状态响应

**文件：** 根 `Makefile`、`Appli/Makefile`、`Custom/Services/Web/api/api_app_admission_module.c`。

- [x] 新增构建选择测试：默认 profile 为 test；production 未注入配置编译后状态为 `unprovisioned`；非法 profile 失败；production test publisher 被拒绝。
- [x] 接入 `APP_TRUST_PROFILE` 与可选绝对路径配置头变量；profile 或注入头 SHA-256 变化会使信任锚对象失效并重编，不改变默认选择。
- [x] 固件 production 编译拒绝 synthetic class、`SYNTHETIC_TEST_ONLY` provenance 和 `ne301-exp-pub`；运行时仍拒绝 test publisher。
- [x] 现有只读 admission 状态响应增加 profile、SPKI SHA-256 和 provenance，便于确认固件编译身份。
- [x] 运行默认 admission API gate 与 production profile 负例。

### 任务 3：合成三签名 Pair 与 #124 gate

**文件：** `tests/app_installer/Makefile`、`test_app_install_gate.c`；新增 per-run synthetic fixture generator 与拒绝用例。

- [x] 生成失败用例：production synthetic profile 下合法旧 `ne301-exp-pub` 包和混合 publisher/SPKI 的 Pair 必须拒绝。
- [x] 先运行失败用例确认其在旧 test-only 锚或未隔离 gate 下会通过（负例对照）。
- [x] 每次运行新生成仅用于测试的 P-256 私钥，在临时构建目录输出 public config 和真实 native、UI、Pair ECDSA 签名 fixture；不落盘生产密钥。
- [x] 使用 production synthetic 锚运行现有 #124 signed Pair、session/run、序号 floor、同序冲突和降级 gate；补充畸形 Pair 与包内 SPKI 摘要替换拒绝。
- [x] 运行 `tests/app_installer` 并记录数字。

### 任务 4：文档、默认构建与交付验证

**文件：** `Docs/design/host-production-trust-selection.md`、`.plans/主机生产发行者信任选择/final-summary.md`。

- [x] 说明 `APP_TRUST_PROFILE`、生产配置头字段、fail-closed 状态与当前无生产身份边界。
- [x] 重跑三套受影响测试并与基线数字比较；验证默认 `make app`、production 无配置 `make app`、test/production/test 档位连续切换和固件编译负例。
- [ ] 检查 diff 的新增代码无注释、无未授权路径、无 key/secret 文件；提交、推送、创建 PR、等待 CI 并写 `/tmp/codex_host79_report.md`。

## 验收依据

- H1–H3 与三条 #79 权威评论原文；基线 `a20d055cff0334ec3a483088dc8d80e9ad451e96`。
- 默认测试基线：admission 421 checks、installer 735 checks、platform offline 2 项程序验证，全部退出码 0；默认 installer signed Pair fixtures 在基线标记 `NOT_RUN`。
- 无真实生产 SPKI 时，production identity 和设备门报告 `BLOCKED`，不得标记 PASS。
