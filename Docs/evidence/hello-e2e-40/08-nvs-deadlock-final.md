# 终局诊断：NVS 凭据持久化缺陷与 fail-closed 死锁（Issue #40 附带 A 级发现）

- 时点: 授权擦除重置（User 原话"擦除重置吧"）之后的完整实测；控制台=板 A UART（SLAB_USBtoUART）
- 本文档为 A 修复立项所需的最小事实集；不含任何凭据值

## 1. 授权与执行账目

1. User 批准原文: "擦除重置吧"（擦除 NVS + 出厂重置授权）；
2. 擦除前快照: `nvs_snapshot_erase.bin` SHA-256 `8d9a90c7bfb085667457599b9c5e7d53a27ce31e0496e726f16f5d7bc6e7b07d`；
3. 擦除: 64KB 全 0xFF 写入 0x70080000（`Download verified successfully`），回读全 0xFF（blank 100%）；
4. OTA state（0x70090000）与 FSBL 探针逐字节未变（SHA=阶段 1 备份）；
5. 后续多次断电重启 + 控制台 `factory reset` 实测（结果见 §2）。

## 2. 擦除后的启动序列（控制台实录，多次一致）

```
nvs media state(part=0, state=1, blank=1)
nvs media state(part=1, state=2, blank=0)
[CORE] [ERROR] Admin credential not reliably resolved(new=1, old=1, media=2), session fail-closed, default password not served
[CORE] [ERROR] Config load from NVS failed(rc=-1), session marked unhealthy, no default backfill write, media preserved
[CORE] [ERROR] Admin credential refused: session credential untrusted (fault media or unreadable credential), fail-closed
[CORE] [ERROR] Admin credential not loadable from config manager(rc=-1), auth fail-closed: login and password change disabled, public default not used
```

- `media=2`=HEALTHY、`new=1`=MISSING（auth_password 键）、`old=1`=MISSING（dev_info_password 键）；
- 状态枚举: `json_config_boot_gate.h`（PRESENT=0/MISSING=1/UNKNOWN=2）；media 枚举见
  `storage_media_gate.c`（HEALTHY=2）。

## 3. 代码级死锁（根因）

`json_config_boot_gate.c`:

- `credential_persist_default_allowed(HEALTHY, MISSING)` = **0**（要求 `user_state == BLANK`）→
  非 BLANK 媒体上缺键时**不允许**写默认凭据；
- `credential_trusted_persisted(MISSING, MISSING, HEALTHY, any)` = **0**（BLANK 分支才认 persist 成功）→
  会话不可信 → 登录/改密全部禁用。

即：**媒体非 BLANK + auth_password 键缺失 = 无任何代码内恢复路径**（不能登录、不能改密、
不能持久化默认凭据；`factory reset` 亦被拒，见 §4）。擦除后首启（BLANK）本应走
`persist_default_allowed(BLANK, MISSING)=1` 分支完成初始化，但该次持久化未成功落盘
（后续 boot 读键为 MISSING 且媒体已非 BLANK——首启的某次写破坏了 BLANK 态而凭据键未落地），
从此进入死锁。

## 4. `factory reset` 实测（设计性拒绝，非执行缺陷）

控制台实录:

```
[CORE] [ERROR] Admin credential record write or read-back proof failed, rolling back to previous credential under intent marker
[CORE] [ERROR] Admin credential persist failed, previous credential restored and proven, intent marker cleared, change reported failed and stays all-old
[CORE] [ERROR] Factory reset refused: default admin credential persist failed(rc=-1), previous credential kept
[SERVICE] [ERROR] Failed to reset JSON config: -1
Factory reset failed: -1
```

`json_config_mgr.c:1169` 路径的凭据"写+读回证明"在该板上不稳定（23:41 API 改密同型失败；
23:52 factory reset 同型失败；本机 `factory reset` 再次复现）。读路径正常（283 个存量键
均可离线解析）、删除路径可成功（"marker proven absent"）——写/读回证明环节是缺陷点。

## 5. NVS 影像学（离线 forensic，工具化解析）

- 阶段 1 备份（21:52，旧固件格式）: 283 个活跃键、有效载荷 3132B / 32KB（**远未满**，排除
  -ENOSPC 根因）；`auth_password` = 明文 legacy 形态；`hw_ssid`/`net_password` 为空（站点
  WiFi 从未配置——板 A 有线网口为 wn=W5500）；`dev_info_fw_ver`="4.3.1.328"（更早部署残留）；
- 擦除前快照: 多出 `auth_password_pending`（意图标记）+ `A1:` PBKDF2 记录（**经 PBKDF2-HMAC-SHA256
  计算验证，该记录编码的密码 = 23:41 测试改密值**）——证明 NVS 写在部分会话中**能够成功**，
  失败是间歇性/顺序性的（写成功而 intent 清除不可证明 → 会话 fail-closed）；
- 修复方向提示（供 A 参考）: `storage_nvs_write`（storage.c:958）→ `nvs_write` → flash_ops
  写/读回链路在 XSPI NOR（boot 识别 MX66UM1G45G/MFI=0xC2）上的写-读回一致性问题；
  `w5500` 断连（"W5500 is disconnected or reset!"，每次启动复现）为另一独立疑点。

## 6. 对 #40 验收的最终影响

- AC1/AC2 真机闭环: **BLOCKED**（设备凭据子系统 fail-closed，任何密码/任何 API 认证均被设
  计性拒绝；这是 #36/#37 防护逻辑按设计工作，触发它的底层是存储写/读回缺陷）；
- 设备当前状态: APP/WEB = 本卡 candidate 固件（4.3.1.459/1.5.0.8，运行正常、SoftAP/控制台/
  安装器 API 齐备）；NVS = 已擦（User 授权）+ 若干启动期写残留；OTA/FSBL/AI/Web 外分区
  均未触碰；APP1/APP2 旧固件备份完整可恢复（03 号证据）；
- User 恢复路径: 修复存储缺陷后 `factory reset` 即可回归出厂态；或 SWD 回写阶段 1 的 NVS
  备份（`5d1c90fc…`）+ APP 分区备份回归旧固件。
