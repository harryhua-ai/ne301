# 终局诊断：NVS 凭据持久化缺陷与 fail-closed 死锁（Issue #40 附带 A 级发现）

- 时点: 独立授权的 NVS 擦除及出厂重置验证之后；控制台为板 A UART（接口标识不公开）
- 本文档为 A 修复立项所需的最小事实集；不含任何凭据值

## 1. 授权与执行账目

1. 授权来源：设备所有者对 NVS-only 擦除及随后出厂重置验证的独立分项授权；设备所有者 → 协调者（私人渠道）→ 本 Claim 执行者。协调者留存原授权记录，A 可经协调者独立核对；授权原文和精确消息时间不复制到公开仓库。详见 04 §4；该授权独立于 Issue #40 的通用 READY。
2. 擦除前快照：`nvs_snapshot_erase.bin` 的 SHA-256 记录在仓外证据中；公开材料不含快照字节或凭据内容。
3. 执行：仅擦除 NVS 分区（0x70080000，64 KiB），全区写为 0xFF 并回读确认；没有全片擦除。
4. 擦除时间：公开记录没有精确分钟。先后锚点是擦除前快照在先、擦除后启动日志与 23:52 出厂重置失败观测在后。
5. 后续：多次断电重启及控制台 `factory reset` 观测见 §2–4；运行期写入、回滚尝试与残留状态均纳入当前账本。
6. OTA state（0x70090000）与 FSBL 探针逐字节未变（SHA=阶段 1 备份）。

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

## 5. 擦除前 NVS 状态（离线核对）

- 阶段 1 备份（21:52）：283 个存量记录、有效载荷 3132B / 32KB，见 03 §1。公开证据只保留计数与摘要，不包含 NVS 原始字节或任何凭据值。
- 擦除前快照：存在凭据状态更新与意图标记的运行期写入痕迹；具体键值、记录格式、凭据摘要和原始字节不公开。该快照证明 NVS 在擦除前已经发生运行期变化，且部分写入曾成功。
- 擦除后启动与重置写入/回滚结果见 §2–4；当前事实是擦除后运行期写入未能可靠恢复凭据，不能用擦除前镜像状态代替擦除后的状态。

## 6. 对 #40 验收的最终影响

- AC1/AC2 真机闭环: **BLOCKED**（设备凭据子系统 fail-closed，任何密码/任何 API 认证均被设
  计性拒绝；这是 #36/#37 防护逻辑按设计工作，触发它的底层是存储写/读回缺陷）；
- 设备当前状态: APP/WEB = 本卡 candidate 固件（4.3.1.459/[network address redacted]，运行正常、SoftAP/控制台/
  安装器 API 齐备）；NVS = 已擦（User 授权）+ 若干启动期写残留；OTA/FSBL/AI/Web 外分区
  均未触碰；APP1/APP2 旧固件备份完整可恢复（03 号证据）；
- User 恢复路径: 修复存储缺陷后 `factory reset` 即可回归出厂态；或 SWD 回写阶段 1 的 NVS
  备份（`5d1c90fc…`）+ APP 分区备份回归旧固件。
