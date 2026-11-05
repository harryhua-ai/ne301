# D1 拆分依赖闭合（修正轮）— app_installer.c 合并编译单元穷尽核查

应 A REQUEST_CHANGES（评论 6655434960）阻断 1 修正：替换 R0 首轮「跨文件 static 仅 2 个」的错误结论。
固件 @ `cfb90ed22ecf3cf750ce08799140c2789e38df27`；TU = `app_installer.c`（宿主，`#include` 于尾部 `:1305-1307`）→ 依序并入 `app_sidecar_verify.c`、`app_sidecar_lifecycle.c`、`app_pair_installer.c`。本文件只做证据闭合与接口方案，不实施拆分。

## 0. 方法（可复现）

TU 顺序不对称性：installer[1..1304] 先于三个 include → installer 的 static/宏对下游可见（无需声明）；反向仅能靠 installer 前部前向声明（实际存在 `:47-51` 两处）。核查覆盖四类对象 × 双向：static 函数、文件级 static 数据（含 `static const` 数组）、宏（含 `#if/#error` 编译守卫）、文件作用域 typedef/struct/enum；另查同名 static 冲突类。复现脚本（仓库外执行，逻辑如下，Python3 仅用 re/标准库）：

```sh
cd Custom/Services/AppHost
# 1) static 定义面（注意多行签名：名字可在下一行，如 installer.c:47-51 的前向声明）
grep -nE '^static' app_installer.c app_sidecar_verify.c app_sidecar_lifecycle.c app_pair_installer.c
# 2) 逐名双向引用（词边界，剔除定义/声明行与注释）——等价于对每个名字跑：
grep -nE '\b<name>\b' <其余三个文件>
# 3) 宏与守卫
grep -nE '^#define|^#if |^#error' app_installer.c app_sidecar_*.c app_pair_installer.c
# 4) 文件作用域 typedef
grep -nE '^typedef|^} [a-z_]+;' app_installer.c app_sidecar_*.c app_pair_installer.c
```

计数基线：四文件 static 定义合计 **140 个函数 + 16 个文件级数据/常量**（installer 27 fn + 6 data；verify 17 fn + 5 data——`static const` 数组 SC_MAGIC/SC_UNSIGNED_MAGIC/SC_PRODUCT_VERSION_PREFIX·SUFFIX/SC_SHA_K，均 verify 局部；lifecycle 90 fn + 5 data；pair 6 fn + 0 data）。四文件共定义 **100 个宏**。同名 static 跨文件仅 §1.5 所列 2 组（声明/定义对）。

## 1. 跨文件闭合结果（穷尽）

### 1.1 static 函数：6 个双向依赖（全部在 installer ↔ lifecycle 之间；verify、pair 双向均为零）

| # | 函数 | 定义 | 反向引用（调用点） | 方向与机制 |
|---|---|---|---|---|
| F1 | `inst_sidecar_revoke_for_native` | lifecycle.c:2886（installer.c:47-49 仅前向声明） | installer.c:854（install 提交路径） | installer→lifecycle，靠 `:47` 前向声明成立 |
| F2 | `inst_uninstall_intent_blocks_boot` | lifecycle.c:2039（installer.c:50-51 仅前向声明） | installer.c:661,905,1001,1117,1165（5 处入口门） | installer→lifecycle，靠 `:50` 前向声明成立 |
| F3 | `inst_sha256` | installer.c:87 | lifecycle.c:976,1882,3052（3 处） | lifecycle→installer，仅靠 TU 顺序（installer 在前）成立 |
| F4 | `inst_kv_delete_proven` | installer.c:314 | lifecycle.c:2216 | 同上 |
| F5 | `inst_record_delete_proven` | installer.c:344 | lifecycle.c:2215 | 同上 |
| F6 | `inst_record_load` | installer.c:357 | lifecycle.c:1901,2475,2619,2650,2817（5 处） | 同上 |

### 1.2 文件级 static 数据：2 个（lifecycle → installer 状态标志，均为写/读语义敏感点）

| # | 数据 | 定义 | 引用 | 所在函数（lifecycle 侧） |
|---|---|---|---|---|
| D1 | `g_boot_resolved` | installer.c:40 | lifecycle.c:2809（读，判 boot 是否已 resolve）、2880（写 FALSE） | `app_install_uninstall_confirmed`（def lifecycle.c:2759，声明于 app_installer.h:157） |
| D2 | `g_state_unknown` | installer.c:41 | lifecycle.c:2879（写 FALSE） | 同上（卸载收尾清障） |

### 1.3 宏与编译守卫：1 个 + 1 守卫

- `APP_INSTALL_PENDING_BLOB_SIZE`（def installer.c:29，宏链 `:26-28` 依赖头文件 `APP_INSTALL_RECORD_BLOB_SIZE`）→ lifecycle.c:2217（卸载清理时按该尺寸删 KV）。四文件 100 个宏中**唯一**跨文件使用者。
- 编译守卫：installer.c:33-38 两处 `#if APP_INSTALL_{RECORD,PENDING}_BIN_SIZE != … #error` 将 .c 本地宏链与 `app_installer.h` 常量绑定——拆分后宏链与守卫必须随迁同一内部头，否则守卫失效或编译断裂。

### 1.4 typedef/struct/enum：0 个跨文件

`app_sidecar_sha256_t`（verify.c:5-10）与 `sc_store_*`/`sc_uninstall_intent_t`（lifecycle.c:93-138）均仅本文件使用（逐名 grep 双向 = 0）；installer、pair 无文件作用域 typedef。

### 1.5 同名冲突类：0（除 2 组声明/定义对）

static 名字同名跨文件仅 `inst_sidecar_revoke_for_native`、`inst_uninstall_intent_blocks_boot`（即 §1.1 F1/F2 的声明对）。verify 与 lifecycle 共用 `sc_` 前缀命名习惯但无同名；拆分后各自保持 static，无外部链接冲突。

### 1.6 头文件治理的外部链接（拆分安全，无需动作）

非 static 跨文件调用均经既有头声明（例：`app_install_uninstall_confirmed` 声明 app_installer.h:157、实现 lifecycle.c:2759；`app_native_vnext_floor_*` 声明 app_native_vnext_floor.h、实现于 lifecycle.c；pair/verify 对外符号同理）。拆分后走正常链接，行为不变。

## 2. 可拆分最小接口方案（建议，未实施）

新建**一个**内部头（第一方命名，无迭代代号；建议 `app_installer_internal.h`，仅 installer.c 与 app_sidecar_lifecycle.c 需要；verify/pair 零依赖、直接改 Makefile 即可独立成编译单元）：

1. **F3–F6（lifecycle 用 installer 的存储层助手）**：在内部头声明并去掉 `static`（属 installer 拥有的 proven 存储工具：SHA256、记录加载/删除、KV 删除）。
2. **F1–F2（installer 用 lifecycle 的门函数）**：把 installer.c:47-51 的两处前向声明移入内部头，定义处去 `static`。
3. **D1–D2（状态标志）**：不改导出可见性——由 installer 提供两个微小访问器（如 `app_install_boot_state_{query,reset}` 风格命名，具体名由实施 issue 定），lifecycle 的 3 个语句改调访问器；标志本身保持 installer 内 static（避免把状态可写面扩大成全局）。
4. **宏+守卫**：installer.c:26-38（`APP_INSTALL_PEND_*` 链 + 两处 `#if/#error`）整体迁入内部头。
5. 构建：`Appli/Makefile` +3 个 `C_SOURCES`；13 个测试 Makefile 的 `app_installer.o` 依赖行同步（各自补独立 `.o` 目标）。

## 3. 修正后的风险分级

- 由「中」上调为 **中-高**：缝不止函数——含 2 个**状态标志写点**（位于卸载收尾语义路径）与 1 个**尺寸宏+编译守卫**（持久化 blob 布局，属 §2.2 事务恢复兼容面）。机械性仍好（全部缝已穷尽枚举、无隐藏通道），但行为等价的举证责任更重。
- 对应最低验证升级：M2.2 增补「卸载收尾三语句（lifecycle.c:2809/2879/2880）语义等价」与「守卫迁移后 `#error` 仍生效（人为错配常量应编译失败）」两条显式断言；M2.1 的符号集对比扩大到 4 个对象文件。
