# 受控 hello-app V1→V2 Web 真机端到端验收报告（Issue #40）

- claim: `harryhua-ai-20261019T131308-cc64fc5e`
- 分支: `agent/40/c8db4b56`（base `98e61bd5` = `experiment/app-host-poc` HEAD）
- 执行环境: macOS 主机 + STM32N6570 真机（现场板 A；ST-LINK 序列标识已脱敏）；设备所有者授权及具体操作时间见 04 §4
- 性质: B 实施与验收记录，不是产品安全认证

## 0. 结论速览

| AC | 结果 | 说明 |
|---|---|---|
| AC0（本卡自加，身份配对前置） | **PASS** | 受控签发钥 SPKI SHA-256 与 Host 编译期锚逐字节配对（`06e732b1…`） |
| AC1 Web V1 装/跑→V2 升/跑→卸载闭环 | **NOT_RUN (BLOCKED)** | 设备凭据子系统 fail-closed（存储写/读回缺陷，见 08 号证据）——登录被设计性禁用，管理 API 无法认证；离线等价验证全覆盖（本表） |
| AC2 跨层拒绝闭环 | **NOT_RUN (BLOCKED)**（设备侧）/ **PASS（离线）** | 负例向量七类 + 设备同源验签器/安装器双套件 34/0、55/0；设备侧部分实抄：认证门序（401→403 高权限）实测正确、安装 API 在新固件上存在且被正确拦截 |
| AC3 烧写前后身份/备份/恢复 | **PASS（部分）** | 备份、分区前后身份、重启及新固件启动已验证；app-only 替换与数据恢复闭环依赖 AC1 真机 Web 流程，仍为 NOT_RUN；NVS 擦除及恢复边界见 08、03。 |
| AC4 汇总 | **PARTIAL（本轮账本修订完成，等待 A 对新 SHA 复核）** | 本文档 + Docs/evidence/hello-e2e-40/01–08 |

**最终状态：BLOCKED（部分交付）**——阶段 0/1/2.1 与 2.2 的启动验证全部 PASS；AC1/AC2 真机
闭环被设备侧存储凭据缺陷实质阻塞（**A 级新缺陷，证据完备**，见 08 号证据）。未把任何离线
结果冒充真机 PASS。新固件管理面（Web/API/SoftAP/控制台/安装器/验签门）在缺陷之外的部分
全部实测正常。

## 1. 交付物清单（本 PR 范围，tests/**+Docs/**）

| 交付物 | 位置 |
|---|---|
| 受控重签 hello-app V1/V2 .neapp 包（可提交字节+溯源 sidecar） | `tests/hello_e2e_40/packages/hello-app_v{1,2}.0.0.neapp` |
| AC2 负例向量（错签/失信发行者/错板/错 ABI/同版冲突） | `tests/hello_e2e_40/packages/*_{badsig,foreign,badboard,badabi,altcontent}.neapp` |
| 纯 Python 离线验签器（结构/manifest/签名/锚配对/负例分类/provenance） | `tests/hello_e2e_40/verify_offline_40.py`（34 checks/0 failures） |
| 设备同源 C 套件（真验签器 app_admission.c+app_trust_anchor.c 编译 + 注入 IO 安装器语义） | `tests/hello_e2e_40/test_hello_e2e_40.c` + `Makefile`（55 checks/0 failures） |
| 负例向量生成器（受控钥+fixture 拒签钥，锚自检 fail-closed） | `tests/hello_e2e_40/gen_vectors_40.py` + `neapp_pkg_40.py` |
| 证据 01 身份配对/重签包 | `Docs/evidence/hello-e2e-40/01-identity-pairing.md` |
| 证据 02 设备只读勘察（含板 A/B 澄清补记） | `Docs/evidence/hello-e2e-40/02-device-recon.md` |
| 证据 03 全分区备份 SHA 清单+恢复路线 | `Docs/evidence/hello-e2e-40/03-backup-restore.md` |
| 证据 04 写前四门核对单 | `Docs/evidence/hello-e2e-40/04-write-gates.md` |
| 证据 05 烧录与运行确认 | `Docs/evidence/hello-e2e-40/05-flash-and-boot.md` |
| 证据 06 BLOCKED/NOT_RUN 账目（含 NVS rc=-1 锚点） | `Docs/evidence/hello-e2e-40/06-blocked-ledger.md` |

## 2. 关键身份与数字（复核锚）

- Host 编译期锚: publisher `ne301-exp-pub`，SPKI SHA-256
  `06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda`（`app_trust_anchor.c`）；
  受控签发钥实测同值（仓外受限存储，零入库零外传）
- hello-app 镜像（App #6 审定字节，ne30x-app `agent/6/9a4daa9a`@`79d8534`）:
  V1=`d6eec431c6c17b84770263ce8e8a9d386f9e798d14024f4452f090c87c6e2d03`（与 Issue #2 记录一致）、
  V2=`e8526a1ab0500a49b23d09fabf2f3664823b55de99bef301fe816d39d5041bfd`
- 重签包: V1=`1c13cac2…fe56`(TBS `405b299e…`), V2=`6d00fba2…9d15`(TBS `9ceb46d1…`)
- 烧写镜像: Host=`build/ne301_App_signed_v4.3.1.459_pkg.bin` `6e98de56…6454`（APP_HOST_POC=1）→
  APP1/APP2；Web=`build/ne301_Web_v1.5.0.8_pkg.bin` `8310d525…7fe7` → WEB（构建产物不入库）
- 真机启动确认: 板 A aicam.log 出现 RSR 冷启动横幅 + `app install: mkdir /apps/pkg`（仅新固件
  具备安装器）+ 卷内新增 `/apps/pkg` 目录
- OTA state 两次回读与备份逐字节一致（`dfb26413…`）——全程未触碰

## 3. 复用而非重造（AC4 要求）

- 验签/安装: 直接编译设备实现 `app_admission.c`、`app_trust_anchor.c`、`app_installer.c`
  （host 套件沿用 #29/#30 的 Makefile 拓扑与 mem_io 注入后端语义，未复制任何逻辑）；
- 打包: 复用 ne30x-app P3 受审打包器 `neapp_pack.py`（P2 v1 冻结协议），未另写打包器；
- 恢复验证语义: 复用 #30 写前意图事务与 #36/#37 fail-closed 设计（板 A 启动日志实测到
  #36 凭据迁移路径在跑）；
- 设备操作方法: 复用 #25 真机证据的 loader/分区/双槽结论（本次实测再次成立）。

## 4. 剩余受控实验限制、已知缺陷与不具备能力（如实声明）

1. **A 级新缺陷（本卡首要发现）**：板 A 上 NVS 凭据持久化不可靠（写/读回证明失败，
   `json_config_mgr.c:1169` → `json_config_nvs.c:660`），叠加 #37 boot-gate 死锁
   （非 BLANK 媒体 + 凭据键 MISSING → `credential_persist_default_allowed`=0 且
   `credential_trusted_persisted`=0 → **无代码内恢复路径**，`factory reset` 亦被设计性拒绝）。
   完整证据链见 08 号证据；修复归属 A（建议另立 issue：storage/NVS 写-读回一致性 + boot-gate
   死锁恢复路径）；
2. **wn（W5500 有线口）每次启动报 "disconnected or reset"**（`w5500_netif.c:230`）→ 无线链路
   未建立（板 A 仅 SoftAP 可达）；独立于存储缺陷，需硬件/驱动侧排查；
3. 板 A 每约 94s 进入 SLEEP（`SLEEP: ~93500 ms` 启动日志），对长时间验收操作有干扰——
   本卡未变更任何电源配置（`net_ap_sleep`=0 等原值保留）；
4. **AC1/AC2 真机闭环未完成**：受缺陷 1 阻塞。闭环输入已全部就绪（受控重签包+负例向量+
   HTTP 闭环脚本），缺陷修复后可在授权窗口内直接执行；
5. 本卡不具备/未涉及：生产信任锚供给、安全原生沙箱、App Store、多 App 并发、发布授权、
   V2/V3/V4 协议能力（合同 non-goals）；
6. Web 管理页与 HTTP API 客户端为已合并产品实现，本卡未改动（forbidden 目录零触碰）；
   Web UI 级验证待缺陷修复后补足。

## 5. 分区状态账本与恢复边界

- **擦除前基线**：21:52 离机 NVS 备份见 03 §1；该备份记录 283 个存量项。擦除前运行期变化见 07 §1（23:41 操作）及 08 §1、§5（擦除前快照）。凭据值、原始 NVS 字节和敏感记录内容未公开。
- **APP1/APP2/WEB 写入阶段**：三个写入时间与镜像身份见 05 §2；该阶段没有通过 SWD 直接写 NVS 或 LittleFS。此阶段限定事实不代表本卡后续阶段。
- **NVS 擦除阶段**：设备所有者另行授权的 NVS-only 擦除将 0x70080000 的 64 KiB 写为 0xFF 并回读确认，见 08 §1。擦除发生在擦除前快照之后、擦除后启动日志和 23:52 出厂重置失败观测之前；公开记录没有精确擦除分钟。授权者角色、私人中转路径及对 A 的核对入口见 04 §4。
- **擦除后阶段**：NVS 启动期发生运行写入/回滚尝试，启动状态转为 non-blank 且凭据仍缺失；多次启动和重置失败见 08 §2–4。当前设备 NVS 不能描述为全程未触碰。
- **恢复边界**：APP1/APP2/WEB 与擦除前 NVS 备份均有仓外恢复材料，摘要见 03 §1。若恢复擦除后的状态，必须把 NVS 纳入回退核验；备份含敏感配置，仍保存在仓外。任何恢复写入均需另行授权。
- 板 A LittleFS 的日志和 `/apps/pkg` 目录增量见 05 §3；板 B 未被本卡触碰。
