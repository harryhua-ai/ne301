# NE301 存储启动非破坏性安全设计（Issue #37，REPLAN Contract 2026-10-10）

- 基线：`002e963a4aa07fd7f9690087bec09404df301304`（experiment/app-host-poc tip）
- 本文档是解释性设计文档；按 User 要求，所有新增/修改代码零注释。

## 1. 目标与三态模型

Contract 要求：LittleFS/NVS 启动与异常恢复非破坏；默认初始化仅发生在"整片 NVS_USER
完整可靠空白检查且零 I/O 错误"的可信空白介质上；未知/故障态保留原始字节、拒绝写入
且可观测；健康态正常 GC/保存/旧数据迁移不受影响。

每个受保护介质区域（NVS_FACTORY、NVS_USER、LittleFS 卷）在启动时被分类为三态之一：

| 状态 | 判定条件 | 写入/擦除策略 |
|------|----------|----------------|
| `UNTRUSTED` | 空白探测出现读取错误，或 NVS init 失败（-EDEADLK/-ESPIPE/-EIO 等） | 全部拒绝，原始字节保留，拒绝可观测（日志 + 返回错误） |
| `BLANK` | 完整区域扫描全部为擦除值(0xFF) 且零读取错误，且 NVS init 成功 | 允许（允许现有首次默认初始化与正常写入） |
| `HEALTHY` | 非空白、零读取错误、NVS init 成功 | 允许（正常 GC/保存/迁移；含既有 CLI 调试面） |

关键分类原则（对应 Contract AC3）：
- "全空白"只能由对整片区域的原始字节扫描证明；magic 缺失、仅出厂身份键
  （serial/MAC/HW）、旧 schema 数据、部分写入、损坏一律不是空白证据。
- 无法可靠分类 = UNTRUSTED = 保留字节 + 拒绝，绝不回退默认值、绝不擦除、绝不复位。

## 2. 介质安全门（session gate）

新模块 `Custom/Hal/storage_media_gate.c/.h`（纯 C，无 OS 依赖，可在主机编译）：

- 每分区保存状态 + 状态的按位取反影子副本。查询时两者不匹配即视为
  `UNTRUSTED`。这防止 CLI `mem w` 对单个 RAM 标志的一次写入绕过媒体门
  （`mem w` 本质是任意内存写原语，完全软件防无法达成；双重写属蓄意攻击，
  超出软件门可证明范围，见 §8 残余风险）。
- 区域注册表：`storage_nvs_region_guard(offset,size)` 供 `storage_flash_write /
  storage_flash_erase` 在直写路径上检查目标范围是否与 UNTRUSTED 分区重叠，
  重叠即拒绝。覆盖"底层存储直写"旁路（OTA/系统状态槽等不重叠区域不受影响）。
- `storage_nvs_write / storage_nvs_delete / storage_nvs_clear` 统一先查门。
  这一个收口点覆盖：CLI `fset`/`factory` 族、固件后台写者（timezone /
  wake state / upload flag / WiFi 升级标记）、json_config 逐键与全量回写、
  `factory_config_write`。任何绕过 json_config 的调用方都无法绕过媒体门。

NVS 库层（`Custom/Common/Lib/nvs/nvs.c`）自身新增：

- `nvs_blank_check(fs,&blank)`：经 `fs->flash_ops.flash_read` 对整片区域做
  完整扫描（可注入测试）；任何读取错误返回错误（分类 UNTRUSTED）。
- 启动撕裂门控：`nvs_startup` 回扫 ATE 时遇到"非擦除值且 CRC8 校验失败"的
  条目（撕裂/损坏证据）置 `startup_flags |= NVS_STARTUP_TEAR_SEEN`；此时
  跳过启动尾部的"擦当前扇区 + GC"恢复分支（保留原始字节；扫描定位仍有效，
  正常写入与写路径 GC 不受影响，扇区自然轮转时由合法 GC 清理）。CRC 校验
  通过或全空白的干净状态下，原有恢复 GC 保持不变（健康态合法 GC 保留）。
- 读 I/O 错误在 `nvs_startup` 内已导致 init 失败（现状保留），叠加门后即
  UNTRUSTED。

## 3. LittleFS 启动（AC1）

`storage.c::lfs_mem_init`：mount 失败不再自动 `lfs_format`。改为：

1. 经 `mem_block_read` 对整卷做只读空白探测；
2. 探测读取错误或卷非空白 → 拒绝格式化，保持 unmounted，打印真实状态；
   所有文件操作沿既有 `storage_lfs_isready` 路径失败（明确拒绝）；
3. 卷完整空白且零读取错误 → 视为可信空白介质，格式化并挂载（保留新设备
   现有行为）。

`storage_format()`（显式授权格式化入口：Web 确认 API / CLI `format`）改为
返回 int（0 成功 / 负失败），失败时不再假装成功；调用方按真实结果上报。

Web `POST /api/v1/device/storage/format` 增加显式 `{"confirm":"FORMAT"}`
知情确认；失败响应只陈述事实（结果失败、卷状态未被证明），不声称介质未变、
不诱导盲目重试。CLI `format` 同样输出真实结果。

## 4. NVS 启动（AC2）

`storage.c::storage_init`：NVS init 失败不再整分区擦除 + 复位。改为记录
UNTRUSTED 分类（门拒绝后续写入），启动继续。分区原始字节保留。

分类流程（每分区）：`nvs_blank_check`（读取错误 → UNTRUSTED）→ 空白判定 →
`nvs_init`（失败 → UNTRUSTED）→ 设置门状态。`startup_flags` 含
`NVS_STARTUP_RECOVERY_DEFERRED` 时打印诊断日志。

## 5. 配置加载与首次初始化（AC2/AC3）

`json_config_load_from_nvs`：

- `is_first_boot` 仅当 `storage_nvs_media_state(NVS_USER) == BLANK`。
  magic 缺失/失配不再是首次写授权（老代码将 magic 失配当首启并全量回写
  默认值，会覆盖旧凭据）。
- 读助手区分三态返回：`AICAM_OK`（读到）/ `AICAM_ERROR_NOT_FOUND`
  （键可证明不存在，来自 NVS `-ENOENT` 完整 ATE 回扫）/ `AICAM_ERROR`
  （未知失败：I/O 错误、分区 UNTRUSTED `-EACCES` 等）。
- 逐键默认回写仅在 `AICAM_ERROR_NOT_FOUND` 时执行。未知读失败不再把
  默认值作为最新条目追加（NVS 追加语义下会遮蔽旧值）。
- 凭据键（`auth_password`）：新键 `NOT_FOUND` 且旧键（`dev_info_password`）
  读到 → 合法迁移（写新键，A 首轮审查认可的形态）；仅当已证明全空白
  （first boot）时持久化默认凭据；健康分区两键均可证明缺失 → RAM 保留
  默认值但**不持久化**（避免"默认凭据提升"家族问题，#36 归口）。
- `json_config_save_to_nvs` 聚合所有写入结果：任何键失败返回
  `AICAM_ERROR`（修复全量保存假报成功）。

策略判断收敛在 `Custom/Core/System/json_config_boot_gate.c/.h`（纯函数，
主机测试可直接用真实 NVS 读返回值驱动）：first-boot 授权、逐键回写授权、
凭据回填/迁移授权。对应 A 对旧 Candidate 的三条技术阻塞（可证明缺失 vs
读失败、凭据迁移条件、健康 legacy 键迁移保留）。

## 6. 保留的现有行为

- 可信全空白 NVS_USER：现有默认配置初始化 + Web 登录/改密流程不变
  （REPLAN 明确保留）。
- 健康分区：正常 GC/保存/读取、旧版本缺键回填默认、config_version 前向
  迁移、旧凭据键迁移读取，均保留。
- 显式授权格式化：Web 确认 API、CLI `format` 保留。
- `fset`/`factory`/后台写者在健康/空白分区行为不变；仅 UNTRUSTED 态被门
  拒绝。
- `mem w` 命令保留（任意 RAM 写原语，见 §2/§8）。

## 7. 验证（AC4）

`tests/storage_gate/`：主机隔离故障注入，链接**真实** `nvs.c`、**真实**
`littlefs`（lfs.c/lfs_util.c）与真实决策模块（storage_media_gate.c、
json_config_boot_gate.c），对 RAM 伪闪存注入故障：

1. 健康回归：写入→重挂→读回、扇区轮转 GC；
2. 全空白：分类 BLANK → 允许首次默认写入；
3. 部分出厂身份预置（serial/MAC/HW 键 + 其余空白）：非空白 → 无默认回写；
4. legacy 兼容：旧 schema 键可读、不写 magic、不重置凭据；
5. 读失败：分类 UNTRUSTED、写/擦全拒、原始字节逐字节不变；
6. CRC/ATE 损坏 + 启动恢复擦除被门控跳过（擦除计数不变）；
7. 重启/掉电撕裂：撕裂 ATE 后其他键完好；
8. LittleFS：损坏卷 mount 失败零格式化调用；空白卷可信初始化；显式格式化；
   探测读错 → 不格式化；
9. 双 NVS 分区独立分类（FACTORY UNTRUSTED + USER HEALTHY 各自策略）；
10. 底层旁路：`storage_flash_write/erase` 区域守卫拒绝 UNTRUSTED 分区范围；
    门影子篡改 → 拒绝；
11. 策略链：真实 NVS 读返回值 → boot_gate 决策（缺键回填、I/O 错不回填、
    凭据迁移/默认条件）。

比对原始字节：每个故障用例在注入前后对整片区域做快照对比，证明
"UNTRUSTED 态零字节改变"。空白/健康态允许的合法追加/擦除单独断言。

目标固件交叉构建见 §9/最终报告；主机模拟不冒充真机验证。

## 8. 残余风险与边界

- `mem w` 双写篡改两个影子字或改写代码本身：软件门不可证明防住任意内存
  写；本设计将单标志翻转变为不可用，并保留三态语义。
- 凭据在 UNTRUSTED 态的 RAM 默认值与 Web 认证关系：公开默认口令风险按
  REPLAN 归 #36；本任务只保证介质字节不被破坏、不假报成功。
- 前端（Web/）不发送 `confirm:"FORMAT"` 时该 API 拒绝：Web/ 为 forbidden
  路径，集成归 A/User 另行决策。
- NVS_FACTORY 空白/健康/UNTRUSTED 与 NVS_USER 独立分类；身份键只读使用。

## 9. 涉及文件

- 新增：`Custom/Hal/storage_media_gate.c/.h`、
  `Custom/Core/System/json_config_boot_gate.c/.h`、
  `tests/storage_gate/**`、本文档。
- 修改：`Custom/Hal/storage.c/.h`、`Custom/Common/Lib/nvs/nvs.c/.h`、
  `Custom/Core/System/json_config_nvs.c`、`json_config_internal.h`、
  `Custom/Core/Log/cli_cmd.c`、`Custom/Services/Web/api/api_device_module.c`、
  `Appli/Makefile`（注册两个新源文件）。
- 未触碰 forbidden：FSBL/Frontend/Web/Model/WakeCore。
