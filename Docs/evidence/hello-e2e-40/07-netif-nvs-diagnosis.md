# 附加诊断：静态 IP 不持久与 NVS 写拒绝（Issue #40 现场附带发现）

- 背景: User 在板 A（新固件 4.3.1.459）Web 界面设置静态 IP `192.168.93.111` 后，详情页仍显示
  DHCP/`192.168.0.20`；同期管理员改密在设备端被拒（23:41 本卡 API 实测复现）。
- 性质: 本卡范围内的只读诊断与代码级定位；修复归属 A（如需另立 issue）。

## 1. 实测事实

| 时点 | 动作 | 结果 |
|---|---|---|
| 23:41 | `POST /api/v1/change-password`（默认凭据，Basic） | `INVALID_PASSWORD`——设备端 `auth_mgr_change_password` 失败（NVS 持久化路径） |
| 23:44 | 控制台 `fw_version` | APP 4.3.1.459 / WEB 1.5.0.8（新固件确认） |
| 23:44 | 控制台 `ifconfig` | wn: up, dhcp, `192.168.0.20`（静态未生效）；wl: down；ap: `NE301_2A1509` `192.168.10.10` |
| 23:45 | `GET /api/v1/app/installed`（默认凭据 Basic） | 403 `High-privilege administrator authentication required`（安装 API 存在且被高权限门拦——新固件指纹成立） |
| 23:5x | 三地址探测（93.111/10.10/0.20:80,443） | 仅 SoftAP `192.168.10.10:80` 从本 Mac 可达（200）；wn 所在段本 Mac 不在 L2 |

## 2. 根因链（代码级）

1. `Custom/Hal/storage.c:958 storage_nvs_write`：写前检查
   `storage_media_gate_writable(part)`，untrusted 时打印
   `"nvs write refused by media gate(part=%d, key=%s)…"` 并返回 **-1**——这就是
   `Failed to persist admin credential record to NVS(rc=-1)`（`json_config_mgr.c:1169`）的 -1；
2. `Custom/Hal/storage_nvs_session.c:82 storage_nvs_session_note_result`：任一 NVS 写出现
   媒体级故障 rc（非 -EINVAL/-EACCES/-ENOSPC）→ 会话立即降级
   `STORAGE_MEDIA_UNTRUSTED` → 本次开机内**所有** NVS 写被拒，重启重建会话才恢复；
3. 初始化即降级的可能路径：`storage_nvs_session.c:34/47`（blank probe 读错误 / nvs_init 失败）
   或首次写故障。板 A 每次冷启动后第一个 NVS 写（#36 凭据迁移，`auth_mgr.c` 迁移路径）即失败，
   呈 100% 复现；
4. 配置保存路径同理：`json_config_set_network_service_config` 聚合写（`json_config_nvs.c:52`
   `json_config_agg_merge(agg, json_config_nvs_write_string(...))`——任一 key 写失败整体失败）
   → 静态 IP 配置**持久化被拒**；按 #37 语义设备宁可拒绝也不落半配置，Web 详情页随后读回
   运行时实际状态（DHCP/0.20）——显示与实际一致，非显示层 bug。

## 3. 新引入还是既有缺陷？

- 该门控（`storage_media_gate.c`/`storage_nvs_session.c`/boot gate）为本分支新增
  （#37 storage-boot-safety；counting 分支基线 `0fcb8299` 无这些文件——`git diff counting HEAD`
  实测 `json_config_nvs.c` 变更 ±2928 行、`json_config_mgr.c` ±1325 行）；
- 因此"配置保存失败被如实拒绝并告警"是新固件行为（旧固件同场景为无验证盲写，失败静默或损坏）；
- 但**触发门控的底层首写故障 rc 是否新固件引入，未定论**——需捕获启动期第一条
  `nvs media fault(part=%d, rc=%d)`（console printf，不进 aicam.log）。该行是最终定论所需
  的最后一块实证（已请求现场配合抓取）。

## 4. 对 #40 验收的影响

- 门控 UNTRUSTED 期间：改密→高权限→App 安装闭环被设备**设计性拒绝**（数据保护正确行为，
  非验收障碍的"绕过对象"）；本卡不绕过、不伪造；
- 若重启后会话转 HEALTHY 且无首写故障 → 闭环照常继续；
- 若每次启动都复现首写故障 → AC1 真机闭环被该底层存储写故障实质阻塞，应另立 issue 修
  NVS/存储驱动，本卡记录 BLOCKED 并保留全部已备好的闭环输入（重签包+负例+脚本）。

## 5. 已确认的正面事实（同窗口实测）

- 新固件 Web/API 栈在板 A 完全工作：HTTP 200、登录成功（默认凭据）、
  `/api/v1/app/installed` 401/403 门序正确（先认证后高权限）——P5 管理面在新固件上行为正确；
- SoftAP `NE301_2A1509`（192.168.10.10）开箱可用，Web 经 SoftAP 全功能可达；
- 控制台 CLI 在新固件上正常（fw_version/ifconfig/flag 可查）。
