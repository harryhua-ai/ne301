# 阶段 1 证据：设备只读勘察（Issue #40，无任何写入）

> ** boards 澄清（2026-10-19 深夜补记，详见 05 号证据 §1）**：本文件 §2 的 CLI 勘察
> （version/fw_version/show_slot/sysinfo）经后续证据链确认来自**板 B**（USB 串口所接板），
> 而非本卡烧写对象**板 A**（ST-LINK 目标板）。两块板为同产品线同代构建（板 A 备份镜像与
> 板 B CLI 所示版本一致、分区布局同为 mem_map.h 布局），勘察数据对分区表/产品形态仍然有效，
> 但其中"板 A 运行 4.3.1.367"的表述不成立——板 A 的真实初始状态以其 21:52 备份镜像为准
> （APP1=4.3.1.364 代际、APP2=4.3.1.367 代际 counting 构建，见 03 号证据 SHA 清单）。

- ST-LINK: SN `56FF6F064984524928381287`，FW `V2J46S7`（接板 A）
- 工具: STM32CubeProgrammer v2.19.0；外存 loader `MX66UW1G45G_STM32N6570-DK.stldr`
  （0x70000000 区间读写必须挂 loader，实测）
- 板 B 控制台: UART @115200（CP2102N），需拉 DTR；CLI 提示符 `AICAM>`

## 1. 枚举与连接状态

| 时点 | ST-LINK 枚举 | SWD HOTPLUG 结果 |
|---|---|---|
| 拨码前（运行位） | 可见（Probe 0, V2J46S7） | `Error: Unable to get core ID`（固件运行态，符合预期） |
| 拨码后（烧录位，User 现场操作） | 可见（同一 Probe） | `Device ID 0x486 / STM32N6xx / Cortex-M55`，AP1 连接成功 |

## 2. 板 B 运行位 CLI 只读勘察（21:35，拨码前）

`version`：APP `4.3.1.367`，Build `2026-09-23 16:45:07`，Git `1a947154-dirty`，Branch `counting`
`fw_version`：FSBL `1.0.3.1` / APP `4.3.1.367` / WEB `1.5.0.7` / WAKECORE `0.2.7.3` / MODEL `4.0.0.0 (AI_2)`
`show_slot`：APP 槽0=4.3.1.364(PV)、槽1=4.3.1.367(ACTIVE,boot_success=1)；WEB 双槽 1.5.0.7；
MODEL 槽0=4.0.0.0。`upgrade_manager.c`：WEB/AI/WiFi 为单槽（slotA==slotB 同址），仅 APP 真双槽。

## 3. 分区表权威源（`Custom/Common/Inc/mem_map.h`，128MB 布局，与 #25 真机证据一致）

| 分区 | 基址 | 大小 | 本卡处置 |
|---|---|---|---|
| FSBL | 0x70000000 | 512K | 只读备份，永不写 |
| NVS | 0x70080000 | 64K | 只读备份，永不写 |
| OTA | 0x70090000 | 8K | 只读备份，永不写（22:47/22:56 两次回读与备份逐字节一致） |
| SWAP | 0x70092000 | 64K | 只读备份，永不写 |
| Reserve1 | 0x700A2000 | 376K | 不触碰 |
| **APP1** | 0x70100000 | 4M | 唯一计划写入目标（Host 槽 0）|
| **APP2** | 0x70500000 | 4M | 唯一计划写入目标（Host 槽 1，消除双槽歧义）|
| AI_1 | 0x70900000 | 8M | 只读备份，永不写 |
| AI_2 | 0x71100000 | 8M | 只读备份，永不写 |
| **WEB** | 0x71900000 | 1M | 唯一计划写入目标（基础 Web，单槽）|
| WiFi FW | 0x71A00000 | 3M | 只读备份，永不写 |
| LittleFS | 0x71D00000 | 96M | 完整只读备份；业务 App 字节仅经设备 HTTP 安装事务进入 |

## 4. 已装签名身份与 App 安装状态（烧写前）

- 板 A 备份 LittleFS（21:59）**无 `/apps` 目录**、NVS 备份无 `app_install*` 键 → 烧写前
  设备无任何已装可信 App 或 signer 身份记录；
- NVS 含 `auth_password` 等键（证据一律脱敏，本卡不读其值）。

## 5. Host 侧软件产物身份（阶段 0 构建）

| 产物 | SHA-256 | 说明 |
|---|---|---|
| `build/ne301_App_signed_v4.3.1.459_pkg.bin` | `6e98de5619c2ae936245f8b763a9f7e452822d633084a1209e4f9e61c3606454` | 本分支 `make pkg-app APP_HOST_POC=1`；含 `/api/v1/app/install*`、`ne301-exp-pub` 锚、安装器符号；OTA 头 `OTAU`，payload 4,075,456 B |
| `build/ne301_Web_v1.5.0.8_pkg.bin` | `8310d5259aacbef27003f7e8d551a1a4cafa9055d19a7370bfd34af38b8b7fe7` | 本分支 `make pkg-web`；前端含 applicationManagement 页（install/installed/run/uninstall 客户端齐全） |

两产物为 candidate 分支构建（不入库，仅烧录输入与 SHA 留痕）。
