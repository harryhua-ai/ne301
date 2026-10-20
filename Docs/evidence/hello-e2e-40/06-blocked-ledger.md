# 阶段 2.2–2.4 BLOCKED/NOT_RUN 账目（Issue #40）

状态口径：本文件只陈述"尝试了什么、看到什么、为何停"，不声称任何未验证的成功。

## 1. 已达成（对照 AC）

| 项 | 结果 | 证据 |
|---|---|---|
| 受控签发身份与 Host 锚配对 | PASS | 01 号证据（openssl SPKI=锚；套件 34/0、55/0） |
| hello-app V1/V2 受控重签包 + 负例向量 | PASS | 01 号证据（包字节已入仓 `tests/hello_e2e_40/packages/`） |
| 全分区离机备份 + 恢复路线 | PASS | 03 号证据（11 分区 SHA 清单；APP1 双读一致；LittleFS 离线挂载） |
| 写前四门 | PASS | 04 号证据 |
| Host+基础 Web 三组件烧写 | PASS | 05 号证据 §2（三次 `Download verified successfully`；回读头=包头） |
| 写入跨复位持久 | PASS | 05 号证据 §2（两次 NRST 后回读头不变；OTA state 与备份逐字节一致） |
| 新固件真机启动 | PASS | 05 号证据 §3（aicam.log：RSR 冷启动横幅 + 安装器 mkdir /apps/pkg + #36 凭据迁移日志；卷内新增 /apps/pkg） |
| 烧写→Web 真机 V1/V2 闭环（AC1 主体） | **NOT_RUN** | 设备网络/控制台通道未建立，见下 |
| AC2 设备侧负例闭环 | **NOT_RUN** | 同上（离线负例矩阵已由 34/0+55/0 套件覆盖，但那不替代真机） |
| AC3 应用替换与数据保留闭环 | **NOT_RUN** | 依赖 AC1 真机 Web 闭环；备份/回退/前后分区身份已验证 |
| AC4 汇总 | **PARTIAL（账本已修订，等待 A 对新 SHA 复核）** | Docs/design/hello-e2e-40-report.md |

## 2. 网络通道尝试账目（全部只读）

| 尝试 | 时间 | 结果 |
|---|---|---|
| 板 B CLI（CP2102N，AICAM>） | 21:35 | 正常应答（version/fw_version/show_slot/sysinfo）——烧写前 |
| 同上 | 22:08 起 | 无响应（0 字节，无回显，发字节无反应）；后续多轮重试同 |
| USB-CDC 控制台 CLI（usbmodem） | 22:08–23:31 | 仅日志流（wl2 每 ~2.5s），无提示符；ifconfig/net info/version 均无应答 |
| USB 枚举判网 | 22:0x | 设备 USB 为 CDC 控制台（0x0483:0x5740），无网络功能（ECM 属 4G 模组侧） |
| 网络与服务发现扫描 | 23:1x–23:3x | 未发现板 A 端点；ARP/mDNS 无新设备；WiFi 无新增热点 |
| 板 A 断电冷启动 #1（22:32，User 操作） | — | 成功启动新固件（aicam.log RSR 横幅）；当时未做网络扫描（23:1x 扫描时已被 NRST 中断） |
| SWD NRST 复位 ×2（22:56/22:59，本卡） | — | 板 A 重入启动流程（aicam.log 1970 组日志）；RTC 未同步 |
| 板 A 断电冷启动 #2（23:3x，User 操作） | — | SWD 转为 `Unable to get core ID`（运行态成立）但网络仍未上线 |
| ST-LINK hardRst（运行位） | 22:21 | 不可用（回退软复位，设备无扰动） |

## 3. 停下原因（满足"结果不符即停"）

- 板 A 跑新固件已证实，但其网络自始未上线、控制台未接入：AC1/AC2 的"管理员 Web 真实闭环"
  需要向设备 Web API 提交请求，当前不存在任何可达通道；
- 现场操作经协调侧中转，多次报告的动作与设备实证状态不一致（如"USB 已重插"而我方捕获
  进程全程存活、日志连续无断点），继续盲试违背"不硬试"约束；
- 修改产品代码（如换控制台/网络初始化）超出本卡 scope（forbidden 目录），不做。

## 4. 遗留疑点（如实记录，未下结论）

1. 板 A 文件日志出现 `Failed to persist admin credential record to NVS(rc=-1)`——NVS 写失败
   原因未知（板 A NVS 备份存在，可事后核对该键布局）；与网络未上线是否相关未知；
2. 新固件启动后无任何 netif/wifi/web 日志（旧固件同阶段有服务告警日志）——是日志级别差异
   还是服务初始化未达，未知；
3. 板 B 的 CLI 在 ~22:05 后失效（烧写前正常）——原因未知，板 B 不在本卡范围；
4. 板 A 在 15:57 曾通过 192.168.93 段服务 WS 流（旧固件）——说明硬件与网络路径当日可用。

## 5. 最小恢复建议（非本卡执行）

1. 将板 A 调试串口接入主机（UART9 或板载 ST-LINK VCP）→ `fw_version` 确认、`ifconfig`
   看网口、启动日志定位卡点；
2. 或由现场提供板 A 的历史访问 URL/IP（当日 15:57 曾有浏览器会话）；
3. 任一通道建立后，本卡的 Web 闭环脚本与包已备好（tests/hello_e2e_40/packages/ +
   e2e40.py 流程），可在授权窗口内直接完成 AC1/AC2/AC3 剩余验证。

## 6. 补充实证 A：NVS 凭据持久化失败（rc=-1）的精确锚点

板 A aicam.log 启动序列中的两条日志（原文见 05 号证据 §3）：

```
[CORE] [ERROR] Failed to persist admin credential record to NVS(rc=-1), change not applied
[CORE] [WARNING] Legacy admin credential record upgrade deferred, verification keeps using the legacy credential
```

固件源锚点（本分支）：

- 失败日志打印点: `Custom/Core/System/json_config_mgr.c:1169`（`json_config_set_device_password` 内，
  调 `json_config_nvs_persist_credential_record` 失败后打印；rc=-1 即 `AICAM_ERROR`）；
- 持久化函数: `Custom/Core/System/json_config_nvs.c:660`（写前读旧值→写→读回证明的封装路径）；
- "upgrade deferred" 日志点: `Custom/Core/Security/auth_mgr.c:547`（迁移失败时保留旧凭据继续验签，
  失败不致命、不阻断启动——与日志事实吻合：板 A 启动继续走完了安装器初始化）；
- 同族已知缺陷参照: `Custom/Services/System/system_service.c:196`（power-mode NVS load 失败
  告警——板 A 11:38 启动日志同款出现，属 NVS 后端读路径既有问题家族）；本条的写路径 rc=-1
  是**不同调用点**的新实证，是否与"网络未上线"伴生、抑或 NRST 复位后 NVS 驱动状态问题，
  未定论（无控制台无法进一步定位）。

## 7. 补充实证 B：USB-CDC 刷屏板 ≠ SWD 目标板的论证（回应"CDC 即板 A"假设）

按 User 假设"wl2 0x19 刷屏口即板 A、0x19 即网络未上线症状"复核，现有证据不支持该假设：

1. 日志框架语义（`Custom/Common/Utils/generic_log.c:303-318,366-398`）：控制台与文件两个
   sink 使用**同一个 module_level** 过滤；wl2 的 `LOG_DRV_ERROR` 为 ERROR 级，在默认级别下
   **必然同时进文件与控制台**。若刷屏板=板 A，其 aicam.log 必有大量 wl2 行——实际提取的
   aicam.log（22:32 启动的本次固件会话）wl2 行数为 **0**；
2. 时序证据：22:56:5x 对板 A 的 `UR+HWrst` 读取成功（板 A 被复位/可读），而 USB-CDC 的
   wl2 流跨越该时刻连续无断点（22:56:53→22:57:16，捕获文件在案）；
3. 板 B 自证：其 CLI 在烧写前应答过 `version`=4.3.1.367 counting 构建（与板 A APP 槽备份
   的构建代际一致、但板 B 从未经历烧写）。

保留度说明：若 DRIVER 模块存在非默认 module_level 配置（本分支代码与默认配置未见），上述
第 1 点可被推翻；届时以板 A 控制台实测为准。该假设不影响 BLOCKED 收敛本身（两板无论如何
划分，板 A 的控制台/网络通道在本卡窗口内均未建立）。

## 8. NVS 擦除前后状态账本（A review delta）

- 擦除前基线：21:52 的离机 NVS 备份见 03 §1；08 §5 记录该备份中有 283 条存量记录。
- 擦除前运行期变化：23:41 管理员凭据变更尝试见 07 §1；后续擦除前快照见 08 §1、§5，含有运行期凭据状态写入痕迹。公开报告不包含其值、摘要或原始字节。
- 擦除事件：设备所有者分项授权的 NVS-only 擦除，64 KiB 写为 0xFF 并读回验证；锚点见 08 §1。该操作晚于擦除前快照，且独立于 APP1/APP2/WEB 写入。
- 擦除后：启动记录先报告 blank，随后出现 non-blank media 与凭据缺失；启动期写入/回滚尝试及 factory-reset 失败见 08 §2–4。
- 证据未记录 NVS 擦除的精确分钟；以上用备份、擦除前快照和擦除后日志限定先后，不推断缺失时间戳。
- 因而不能再概括为 NVS 全程未触碰。报告现按上述阶段陈述，私有授权原文与凭据/闪存内容均不公开。
