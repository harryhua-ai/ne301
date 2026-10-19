# 写前证据门（四项）核对单（Issue #40）

状态口径：任一项不成立 → 停在 BLOCKED，不执行任何 Flash 写入。本单在四门齐备后才允许
进入阶段 2。

## 门 ① 精确目标镜像与写入分区清单

写入工具：`STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el MX66UW1G45G_STM32N6570-DK.stldr
-w <file> <addr> --verify`（逐组件，UR 烧录位；**禁止** `make flash` 及其 `erase-ota`）。

| # | 文件（本会话构建） | SHA-256 | 写入地址 | 分区 | 大小 |
|---|---|---|---|---|---|
| 1 | `build/ne301_App_signed_v4.3.1.459_pkg.bin` | `6e98de5619c2ae936245f8b763a9f7e452822d633084a1209e4f9e61c3606454` | 0x70100000 | APP1（槽0） | 4,076,480 B |
| 2 | 同上 | 同上 | 0x70500000 | APP2（槽1） | 4,076,480 B |
| 3 | `build/ne301_Web_v1.5.0.8_pkg.bin` | `8310d5259aacbef27003f7e8d551a1a4cafa9055d19a7370bfd34af38b8b7fe7` | 0x71900000 | WEB（单槽） | 302,316 B |

- APP 双槽写入同一镜像：消除 OTA 槽位歧义（原 ACTIVE=槽1），两次部署等价于一次 Host 替换；
  OTA state（0x70090000）不写不改，恢复路径见门 ②。
- 明确不写：FSBL / NVS / OTA / SWAP / Reserve1 / AI_1 / AI_2 / WiFi FW / LittleFS。
- `.neapp` 业务包不通过 SWD 写入——只经设备 HTTP API（`POST /api/v1/app/install`）进入
  LittleFS，由设备自身验签后提交（这正是 AC1 被验对象，不属于本门"刷写"范畴）。

## 门 ② 完整离机备份摘要与可信恢复步骤

- 备份目录（**仓外**）：`/tmp/n301_bak/`，由 `backup_all.sh` 逐分区 `STM32_Programmer_CLI
  -u` 全量读取；SHA-256 清单见 `03-backup-restore.md`（含 app1 双读一致性复核）。
- 覆盖：FSBL/NVS/OTA/SWAP/APP1/APP2/AI_1/AI_2/WEB/WiFiFW/LittleFS（全部分区，共约 125MB）。
- 恢复步骤（任一异常后的可信回退）：
  1. SW2 拨回烧录位（与备份/写入同模式，`-c port=SWD mode=HOTPLUG -el <loader>`）；
  2. 逐分区回写备份文件：`-w /tmp/n301_bak/<part>.bin <原地址> --verify`（APP1←app1.bin
     0x70100000、APP2←app2.bin 0x70500000、WEB←web.bin 0x71900000 为必恢复三项；其余分区
     本卡计划不写、无需恢复）；
  3. 回写后全量回读比对 SHA-256 与备份清单一致；
  4. SW2 拨回运行位上电，`fw_version`/`show_slot`/`version` 应恢复 4.3.1.367/counting 原状。
- 已知恢复边界：LittleFS 卷若被设备运行时改写（业务 App 安装/日志），恢复到备份点会丢失
  该期间增量——本卡在阶段 2 前后各做一次小卷快照核对，安装产生的 `/apps/pkg/*.neapp` 属
  测试自生产数据，不属于用户数据。

## 门 ③ V1/V2 重签包签名 + SPKI 与 Host 锚匹配已验证

- 见 `01-identity-pairing.md`：openssl 导出受控签发钥 SPKI SHA-256 = Host 锚 `06e732b1…`；
- 两包经（a）纯 Python 验签 34/0、（b）设备同源 `app_admission.c`+`app_trust_anchor.c`
  C 套件 55/0 双通道验证 OK；负例向量拒绝类全部符合预期；
- V1/V2 镜像与 App #6 受审记录逐字节一致（`d6eec431…` / `e8526a1a…`）。

## 门 ④ User 授权覆盖的操作清单与预期数据影响

- 授权来源：Issue #40 CURRENT A 裁决（2026-10-19 READY："User 现明确说明设备具备现场验收
  条件，并允许操作设备进入烧录模式"）+ 本卡任务书（协调侧派发，claim
  `harryhua-ai-20261019T131308-cc64fc5e`）+ User 现场执行 SW2 拨码（烧录位进入已由 User 操作）。
- 授权覆盖的具体操作：
  1. SWD 只读勘察与全分区离机备份（只读）；
  2. 向 APP1/APP2/WEB 三个分区写入上表三个镜像（受控签名产物，写入后 `--verify`）；
  3. SW2 拨回运行位后设备正常启动（Host 固件 + 基础 Web 替换）；
  4. 通过设备管理 Web/API 安装、执行、升级、卸载受控签发的 hello-app V1/V2（数据落在
     LittleFS `/apps/pkg/` 与 NVS `app_install*` 键——均为本测试自生产数据）；
  5. 受控负例上传（验签拒绝类，零写入路径）与受控掉电/重启恢复验证（复用 #30 语义）。
- 预期数据影响：APP1/APP2/WEB 三分区被替换（原 counting 产线构建，可按门 ② 逐字节恢复）；
  NVS/LittleFS 原用户数据不主动擦除；不触碰 AI 模型、WiFi 固件、FSBL、OTA state。
- 不在授权内（禁止）：全片擦除、`make flash`、改产品源码、更换信任根、触碰私钥。

## 核对结论

- 门 ①：本单 §1（镜像 SHA 与地址在烧录命令中逐字使用）
- 门 ②：`03-backup-restore.md` 清单 + 本单 §2 恢复步骤
- 门 ③：`01-identity-pairing.md` §1/§3/§5
- 门 ④：本单 §4
