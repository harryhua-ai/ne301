# mbedTLS Threading 注册门与首启管理员凭据编码失败归因

## 机制（#121）

生产配置 `config-ccm-psk-tls1_2.h`（`Appli/Makefile:651` 注入，Appli 全量编译单元生效）启用
`MBEDTLS_SHA256_ALT + MBEDTLS_HAL_SHA256_ALT + MBEDTLS_THREADING_C + MBEDTLS_THREADING_ALT`。
mbedTLS `library/threading.c` 在 `mbedtls_threading_set_alt()` 调用前将 `mbedtls_mutex_init/free`
预置为 dummy 桩、`mbedtls_mutex_lock/unlock` 预置为返回 `MBEDTLS_ERR_THREADING_BAD_INPUT_DATA`
的 fail 桩；`port/sha256_alt.c` 的硬件 HASH 路径在每次 starts/update/finish 获取 `hash_mutex`。

启动时序（`experiment/app-host-poc@849b94f1`）：

1. `main()` → `osKernelInitialize` → `osThreadNew(StartMainTask)` → `osKernelStart`（`Appli/Core/Src/main.c:504-507`）
2. `StartMainTask` → `framework_init`（无 mbedtls 使用）→ `driver_core_init`（驱动注册，无 SHA 调用）→ `core_system_init`（`main.c:348-362`）
3. `core_system_init` → config 阶段 `json_config_mgr_init`（`Custom/Core/core_init.c:318`）→ 健康 BLANK 首启 gate → `json_config_nvs_persist_credential_record`（`json_config_nvs.c:2611/2613`）→ `auth_mgr_credential_record_encode` → fill_salt `mbedtls_sha256` + `auth_mgr_pbkdf2_hmac_sha256`
4. threading 注册唯一生产路径 = `communication_service_init`（`service_init.c:110` 服务表）→ `communication_service.c:1059` → `netif_manager_register` → `netif_manager_init:1001` → `mbedtls_threading_alt_init()`，晚于 3
5. 其余注册点 `mm_mbedtls_port_init`（`mm_halow_netif.c:1784`）与 hostap DPP（`crypto_mbedtls_dpp.c:315`）同属网络阶段

因此在 config 阶段首次 SHA256 访问时 mutex 函数指针仍为 fail 桩：fill_salt 的 `mbedtls_sha256`
失败（返回值被忽略，盐退化为全零）、PBKDF2 的 `mbedtls_md_hmac` 失败被压平为 -1、encode 返回
`AICAM_ERROR` 且该分支无日志（`json_config_nvs.c:783-789`）→ 默认凭据不落盘；后续启动分区非
BLANK，#37 gate fail-closed 无自愈。该失败发生在任何 NVS 写入之前，与 NVS/Flash 媒体故障无关。

## 修复（最小行为修复）

1. `Custom/Core/core_init.c`：`core_system_init` 进入各阶段前幂等调用 `mbedtls_threading_alt_init()`
   （`threading_alt.c` 自带 inited 守卫；后续网络阶段的重复注册为 no-op）。
2. `Custom/Core/Security/auth_mgr.c`：`fill_salt` 检查 `mbedtls_sha256` 返回值并失败即中止 encode；
   PBKDF2 传播底层 mbedTLS rc（不再压平为 -1）；两处失败路径新增无敏感内容的 LOG。
3. `Custom/Core/System/json_config_nvs.c`：encode 失败分支新增持久化阶段 LOG（rc + NVS untouched），
   消除静默早退。
4. `Custom/Common/Lib/MbedTLS/port/sha256_alt.c`：`hash_mutex` 毒化自愈——注册前首次使用会把单例
   留在 count>0 且 mutex NULL 的状态，注册后首个新上下文 init 时检测 `mutex == NULL` 补初始化。

## 测试分层（tests/threading_gate/）

- 真实编译单元：`library/threading.c`、`library/sha256.c`、`library/md.c`、`port/sha256_alt.c`、
  `port/threading_alt.c`（生产 `MBEDTLS_CONFIG_FILE` 子集：MD_C/SHA256_C/SHA256_ALT/HAL_SHA256_ALT/
  THREADING_C/THREADING_ALT）、`auth_mgr.c`、`json_config_*` 全链。
- HAL 替身：`hal_hash_shim.c` 以软件 SHA-256 实现哈希语义（摘要随输入变化），HAL 寄存器/时序不模拟。
- CMSIS 替身：`tests/storage_gate/shim` 既有 osMutex 桩（单线程语义）。
- 断言：注册前 `mbedtls_sha256` 必失败、BLANK 首启 fail-closed（disposition=FAIL_CLOSED_MEDIA、
  marker ABSENT、无 auth_password 落盘、登录/改密拒绝、NVS 零写入）；注册幂等；毒化 mutex 自愈；
  注册后 BLANK 首启默认凭据全链提交 + 重启可信复验 + 改密轮换后旧密码被拒。

## 验证矩阵（2026-10-20，`agent/121/52f6d9ce` 候选树）

| 套件 | 命令 | 结果 |
|---|---|---|
| threading_gate（新） | `cd tests/threading_gate && make clean && make test` | total=29 fail=0 |
| nvs_credential_recovery | 同型 | total=123 fail=0 |
| storage_gate 四套 | `cd tests/storage_gate && make test` | 802/0、267/0、1960/0、83/0 |

NOT_RUN：板卡实测（HAL HASH 硬件、ThreadX osMutex、真实多线程竞争、完整生产 config 的其余模块）、
BLANK NVS 真机首启（属 #40 现场复测，需 User 授权）。源码/离线 DONE 不等于 #40 真机恢复。
