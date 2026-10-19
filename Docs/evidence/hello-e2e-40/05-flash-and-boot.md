# 阶段 2 证据：Host + 基础 Web 受控烧录与运行确认（Issue #40，AC3 前后镜像身份）

- 前置: 写前四门全 PASS（04 号证据）
- 命令形态（逐组件，无 erase-ota，无 make flash）:
  `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -el MX66UW1G45G_STM32N6570-DK.stldr
   -w <pkg> <addr> --verify`（烧录阶段用 HOTPLUG，同参数成功；后续只读复核改用 UR+HWrst 可重复）

## 1. 目标板澄清（重要，纠正 02 号证据的口述混杂）

现场经 ST-LINK 连接的板（下称**板 A**，本卡烧写对象）与经 USB 串口（usbmodem
`NE302 CDC Console` + CP2102N `usbserial-14130`）输出控制台的板（下称**板 B**）是
**两块不同物理板**。证据链：

1. 22:56:5x 对板 A 执行 SWD `reset=HWrst` 后立即成功读取 XSPI（此前后台应用占 flashy 读失败），
   但 USB-CDC 的日志流（`wl2: Failed to send data frame`，每 ~2.5s）跨越该复位时刻
   **完全无间断**（捕获 22:56:53→22:57:16 连续）——打日志的应用未经历任何复位；
2. 板 A 的文件日志（aicam.log，经 SWD 从板 A LittleFS 提取，见 §3）中 **wl2 错误为 0 条**，
   而 USB-CDC 流被同一错误刷屏——同一错误不可能"控制台有、文件无"（ERROR 级同时写双路）；
3. 板 B 全程未重启（USB 从未断开：我方两个串口捕获进程在 22:33–22:47 全程存活；/dev 节点
   时间戳未变；日志 RTC 时间戳连续）。

因此 02 号证据中"运行位 CLI 勘察（version/fw_version/show_slot/sysinfo）"实为**板 B** 的状态；
板 A 的等价信息以本卡 SWD 只读备份与 aicam.log 为准（其 APP1/APP2 备份内容与板 B CLI 所示
版本系同产品线同代构建，分区布局由 mem_map.h 权威定义，不受影响）。

## 2. 写入记录与前后镜像身份（AC3）

| 时点 | 写入 | 结果 |
|---|---|---|
| 22:00:56 | App pkg → APP1 0x70100000 | `Download verified successfully` |
| 22:02:17 | App pkg → APP2 0x70500000 | `Download verified successfully` |
| 22:03:38 | Web pkg → WEB 0x71900000 | `Download verified successfully` |

| 分区 | 前（备份 SHA-256） | 后（写入包 / 4KB 回读头 SHA-256） |
|---|---|---|
| APP1 | `629cae00…9346` | 包 `6e98de56…6454`；写后回读头 `33a45c30…8826` = 包头 |
| APP2 | `46d5b472…543e` | 同上（双槽同镜像，消除 OTA 槽位歧义） |
| WEB | `be3d2422…8843` | 包 `8310d525…7fe7`；回读头 `46c16879…d013` = 包头 |

**跨复位持久性**：22:56 与 22:59 两次 `UR+HWrst` 复位后回读，APP1/APP2 头 SHA 仍为
`33a45c30…8826`（`rb_app1_now.bin` / `rb_app2_now.bin`）——写入持久、未被任何启动改写。

未写分区（FSBL/NVS/OTA/SWAP/AI_1/AI_2/WiFiFW/LittleFS 直写）零变化；其中 OTA state 在
22:47 与 22:56 两次回读均与备份**逐字节一致**（`dfb26413…`）——本卡全程未触碰 OTA 状态。

## 3. 板 A 运行新固件的确认（aicam.log 提取）

23:00 经 `UR+HWrst` 全量重读板 A LittleFS（96MB，SHA `61284f71…b07c`，对比 21:59 备份
`7809d8ba…db34` 有 32032 字节差异），离线挂载（`tests/app_host/lfstool` 同源 lfs v2.1 语义）：

- 卷内**新增 `/apps/pkg` 目录**——只有含 App 安装器的新固件会创建（旧产线固件无此代码）；
- `aicam.log` 尾部（本卡提取，`/tmp/n301_bak/aicam_now.log`）：
  - `[2026-10-19 22:32:44] [MAIN] RSR=0x01e00000 SFTRST PINRST BORRST PORRST` —— 22:32 现场冷启动横幅；
  - `[CORE] Failed to persist admin credential record to NVS(rc=-1)` + `Legacy admin credential … keeps using the legacy credential` —— #36 管理员凭据基线代码在跑（旧 counting 构建无此日志路径）；
  - `[SERVICE] app install: mkdir /apps failed (may already exist)` ×2 —— **App 安装器初始化**（首次启动创建成功，后续启动已存在）；
  - 末段两组 `[1970-01-01 08:00:00]` 同型条目 —— 对应我方 22:56/22:59 两次 NRST 复位后的再启动（RTC 未同步，属 NRST 复位预期）。

结论：**板 A 冷启动加载并运行了新 Host 固件（含安装器、#36/#37 安全基线），烧写→启动链路真机成立。**

## 4. 运行位残留问题（Phase 2.2/2.3 阻塞点）

- 板 A 的控制台未接 USB（现接 USB 的串口属板 B），无法经 UART 观察/操作板 A；
- 板 A 在 192.168.93.0/24（15:57 曾向 192.168.93.19 推 WS 流，见 aicam.log），但本卡 23:1x
  扫描（80/443/8080 全段 + ARP/mDNS）未发现其 Web 端点；最近两次 NRST 复位后网络未上线
  （疑似 SiWG917 模组需整机断电复位，NRST 不复位模组）；
- 处置：请求现场对板 A 整机断电冷启动后重扫；若仍不上线，Phase 2.3 按 NOT_RUN 收敛并如实报告。
