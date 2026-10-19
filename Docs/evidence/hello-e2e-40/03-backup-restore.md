# 阶段 1 证据：全分区离机备份与恢复路线（Issue #40）

- 备份位置（仓外）: `/tmp/n301_bak/`；备份方式: 拨烧录位后 `STM32_Programmer_CLI -u`
  逐分区全量读取（同一 SWD/loader，读时无任何写命令）
- 读取完成时间: 2026-10-19 21:47–21:59（LittleFS 96MB 约 11.5 分钟）
- 读一致性: APP1 双读复核——两次独立读取 SHA-256 相同
  （`629cae00…346`，见 `/tmp/n301_bak/backup.log` 末段）

## 1. 备份 SHA-256 清单（分区 → 备份文件）

| 分区 | 地址 | 长度 | 备份文件 | SHA-256 |
|---|---|---|---|---|
| FSBL | 0x70000000 | 0x80000 | `fsbl.bin` | `2bccc08e0203e609348d81e953df533de813db694777fc61f1ae07567def9e23` |
| NVS | 0x70080000 | 0x10000 | `nvs.bin` | `5d1c90fc5fd3afd6d7ffd001bbcdd9bb2cd5f88eea7e84e6778e109eae198db5` |
| OTA | 0x70090000 | 0x2000 | `ota.bin` | `dfb264137f4913eedeacc50d717ee08ad79c63b08f5988f65f87f785755e41d3` |
| SWAP | 0x70092000 | 0x10000 | `swap.bin` | `71189f7fb6aed638640078fba3a35fda6c39c8962e74dcc75935aac948da9063` |
| APP1 | 0x70100000 | 0x400000 | `app1.bin` | `629cae0045c7170e94a14c836e4a39eee8c8c7e762303456f3ac0ae6776f9346` |
| APP2 | 0x70500000 | 0x400000 | `app2.bin` | `46d5b472b8b288c3f6f2e231d48fb13a401e21f33101776950bd6e964677543e` |
| AI_1 | 0x70900000 | 0x800000 | `ai1.bin` | `15321ce0fb3c1677689ae8194fd33c492624797e69d7b05e9c2f9a7fa028d90c` |
| AI_2 | 0x71100000 | 0x800000 | `ai2.bin` | `45708be442f440e563e4bc7ce16aadde6f58030da384824d69e09c71b6d17f76` |
| WEB | 0x71900000 | 0x100000 | `web.bin` | `be3d242228f7a717351d817d7b1d7c6244b962ecf86d6481deaaa04a06e68843` |
| WiFiFW | 0x71A00000 | 0x300000 | `wififw.bin` | `53695f18cca71592db1cbaf8702948f421f0add4533eb17fc2dc3b3abeab03be` |
| LittleFS | 0x71D00000 | 0x6000000 | `littlefs.bin` | `7809d8ba9fe04282c9ffec1fa90e5bdbba74334db87da7ddf91a5cfd30d0db34` |

- APP1≠APP2 SHA 与槽表一致（4.3.1.364 / 4.3.1.367 两版构建）；WEB 备份内含
  `NE301_WEB … (1.5.0.7)` OTA 包标识，与 `fw_version` 一致——备份内容与勘察互证。
- 备份镜像**不含**任何对外凭证明文（NVS 凭据为 PBKDF2 记录/迁移遗留；本卡不读不传）。

## 2. LittleFS 卷离线核查（只读挂载备份镜像，`tests/app_host/lfstool` 语义）

`lfstool littlefs.bin - list` → mount 成功（block_count=24576, 4KB 块）：

```
aicam.log / aicam.log.1-.5（运行日志）
captures/（data/index/meta 空文件）
config/config_store.bin (16000B)
config/lc_delivery.bin (399872B)
config/line_counting_totals.bin (48B)
```

- **无 `/apps` 目录**：现产线固件无 App 安装器，设备无任何已装 App 痕迹——与 §02-4 一致。
- 卷内为 counting 业务数据与设备配置；本卡所有阶段**不直接写 LittleFS**，业务 App 字节
  仅经设备 HTTP 安装事务进入该卷。

## 3. 可信恢复步骤（写前证据门 ②）

1. SW2 拨烧录位（与备份/写入同一模式）；
2. 仅对**被写过**的分区回写备份：`STM32_Programmer_CLI -c port=SWD mode=HOTPLUG
   -el MX66UW1G45G_STM32N6570-DK.stldr -w /tmp/n301_bak/app1.bin 0x70100000 --verify`
   （APP2/WEB 同型：0x70500000/0x71900000）；本卡计划外的分区无需恢复；
3. 回写后 `-u` 全量回读，比对 §1 清单 SHA-256 逐字节一致；
4. SW2 拨回运行位上电，`fw_version`/`show_slot`/`version` 应恢复 4.3.1.367/counting 原状；
5. 恢复边界：LittleFS 若被运行时改写（测试安装产生的 `/apps/pkg/*.neapp`、日志），恢复到
   备份点将丢弃该增量——这些是本卡自生产测试数据，非用户数据；用户数据
   （captures/config/日志）在备份点完整可回。
