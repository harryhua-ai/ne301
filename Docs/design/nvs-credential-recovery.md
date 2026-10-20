# NVS 管理凭据首启持久化与 fail-closed 受控恢复

状态: 提案（Issue #119 candidate）· 关联: #36 / #37 / #40（08 号证据）
范围: 软件行为契约与离线验证结果；板级复测项见文末清单

## 1. 问题

板上实证（#40 板 A，三连冷启动复现）：管理员凭据首次持久化的
"写 + 读回证明 + 意图标记清除"链路在写/读回证明环节失败
（`json_config_mgr.c` persist 路径, `storage.c` media gate 拒写
rc=-1）后，媒体转为非 BLANK 且 `auth_password` /
`dev_info_password` 均缺失；`json_config_boot_gate` 的
BLANK+MISSING 才允许默认凭据落盘语义使该状态无代码内恢复路径，
登录 / 改密 / factory reset 全部设计性拒绝（fail-closed）。

## 2. 行为边界（本次变更后）

1. **稳定读证明**: 凭据所有权相关读全部要求两次独立读结果
   一致（`read_stable`）——覆盖意图标记存在性读取（boot
   resolve）、当前/legacy 凭据状态读取（boot 加载）、前值捕获
   （persist 入口）、提交后读回验证、写证明与删除证明；不一致
   归类 `UNSTABLE_READ` 并按未证明处理。单次读毛刺不再能翻转
   凭据所有权（false-absent 的意图标记不会跳过 resolve，
   false-MISSING 的前值捕获不会丢失去回基准）。
2. **故障分类**: 凭据持久化故障细分为 `GATE_REFUSED` /
   `WRITE_FAIL` / `VERIFY_UNREADABLE` / `VERIFY_MISMATCH` /
   `UNSTABLE_READ` / `MARKER_UNRESOLVED`，经
   `json_config_nvs_credential_last_fault()` 可查询，日志逐类
   可辨识；错误传播来源（门控 / IO / 读回）可归因。
3. **事务顺序不变**: 意图标记 → 写凭据 → 读回验证 → 清标记
   （全部带证明）；任一未证明即回滚或保持 undecided，重启由
   `intent_resolve` 收敛到标记所记录的前一凭据。数据保全、
   拒绝未知凭据、非 BLANK 不重发默认口令等 #36/#37 已验收
   语义一律保留。
4. **启动处置报告**: 每次启动输出凭据处置
   （`disposition` 枚举: TRUSTED_RECORD / TRUSTED_MIGRATED /
   TRUSTED_FIRST_BOOT_DEFAULT / FAIL_CLOSED_MEDIA /
   FAIL_CLOSED_MARKER_UNRESOLVED /
   FAIL_CLOSED_COMMIT_MISSING_NONBLANK /
   FAIL_CLOSED_UNKNOWN_KEY），经
   `json_config_nvs_credential_last_disposition()` 可查询。

## 3. 死锁状态（非 BLANK + MISSING/MISSING）的受控处置

软件无法从该状态证明恢复权（无法区分"凭据被删"与"媒体写/
读回损坏"），因此：

- 保持 fail-closed：登录、改密、factory reset 中的默认口令
  重生均拒绝（#36/#37 语义不变）；
- 启动处置报告输出
  `FAIL_CLOSED_COMMIT_MISSING_NONBLANK` 并明确指向外部
  有权操作者流程（本文档 §5）；
- 不添加任何自动恢复、默认口令旁路或无认证远程重置入口。

## 4. 与 #40 真机阻断的因果对应

- 板 A 首启默认凭据事务在读回证明环节失败（写成功而标记清除
  不可证明 → undecided → 标记留存）→ 后续 NRST 复位污染会话
  加剧 → 非 BLANK+MISSING 死锁。本次变更使该路径的每一步
  可分类、可查询、可复盘（fault class + disposition）；
- 硬件层写/读回不一致（XSPI NOR MX66UM1G45G）根因 **UNKNOWN**，
  本变更不主张修复硬件行为，仅提供证明稳健化（双读一致）与
  精确归因。

## 5. 外部有权操作者恢复流程（现场 runbook）

1. SW2 置烧录位（SWD 可用）；ST-LINK 连接；
2. 全分区离机备份（参照 #40 阶段 1 runbook）；
3. 恢复 NVS 分区备份（0x70080000, 64KB），或整片擦除 NVS
   分区使其回到 BLANK（触发首启默认凭据流程）；
4. SW2 回启动位、断电重启；
5. 出厂默认凭据登录（首启改密流程）。

## 6. 离线验证（tests/nvs_credential_recovery，r2 候选实测 123 项全过）

执行记录：候选 `3ee79083e392d4e6150e98d0dd90373d0770e601` 树内
`cd tests/nvs_credential_recovery && make clean && make test` →
`nvs_credential_recovery: total=123 fail=0`（含 r2 新增
false-absent 意图标记与前值捕获毛刺两项边界回归）。

| AC | 覆盖 |
| --- | --- |
| AC1 | 故障注入区分 init/read、first write、read-back、标记清除/恢复、gate 降级路径（fault class 断言） |
| AC2 | BLANK 首启成功、legacy 迁移；持久+读回+标记清除全部证明才 trusted；重启一致 |
| AC3 | 非 BLANK+MISSING 死锁复现 → `FAIL_CLOSED_COMMIT_MISSING_NONBLANK` + 数据保全 + runbook 指向；无自动恢复 |
| AC4 | 首写不完整 / 读回错误 / 读不稳定 / 标记无效 / gate 拒绝 / 权限拒绝 / 重启一致；重启时 transient false-absent 意图标记（未提交凭据不暴露）与首读毛刺前值捕获（旧凭据保全）；与 #37 既有四套件（802+267+1960+83）同树全过 |
| AC5 | 本文 + 行为边界（§2/§3） |

## 7. 板级复测清单（NOT_RUN，待独立设备授权）

1. 板 A 修复固件烧录后：死锁状态启动处置报告实录；
2. 外部 runbook §5 实操：NVS 恢复 → 首启默认凭据 → 改密；
3. 真机 Web 闭环（#40 AC1/AC2/AC3）：V1 装/跑 → V2 升/跑 →
   负例 → 卸载 → 重启恢复；
4. 写/读回不一致的硬件复现率统计（根因归属 STILL UNKNOWN）。
