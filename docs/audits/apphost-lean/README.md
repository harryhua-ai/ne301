# Lean AppHost 双仓依赖闭合与结构审计（R0 / D0）— 证据索引

Issue: harryhua-ai/ne301 #181（[Manual GO][Research-only]，单次例外，无 ght、无 PR、无合并）
上位需求: #172。本审计为只读调研结论，不构成任何 Candidate；文件移动不等同于瘦身或提速。

## 基线与漂移记录（复核时间 2026-11-05T09:47:17Z）

| 仓库 | 基线 SHA（capsule/issue 约定） | 实测 tip（本次复核） | 漂移 |
|---|---|---|---|
| camthink-ai/ne301 `app-host`（固件审计源） | `cfb90ed22ecf3cf750ce08799140c2789e38df27` | `cfb90ed22ecf3cf750ce08799140c2789e38df27` | 无（0 commit） |
| camthink-ai/ne30x-app `main`（App 审计源） | `460c8cd17c45d04aedf1cde0af4a8123502d91d7` | `460c8cd17c45d04aedf1cde0af4a8123502d91d7` | 无（0 commit） |

- 调研分支：`research/lean-apphost-r0-181`（fork harryhua-ai/ne301），起点 HEAD = `cfb90ed22ecf3cf750ce08799140c2789e38df27`，与上游 `app-host` tip 完全一致。
- App 侧审计在只读 detached checkout `/Users/harryhua/n301-dispatch/appsrc-460c8cd1` 上进行，未在 ne30x-app 仓内做任何写操作（无 branch/commit/push）。
- 漂移复核命令（可重放）：
  - `git ls-remote https://github.com/camthink-ai/ne301.git app-host`
  - `git ls-remote https://github.com/camthink-ai/ne30x-app.git main`

## 路径大小写规范化披露（对 Issue 合同的偏离，已按 capsule 指示执行）

Issue 合同写允许提交路径为 `Docs/audits/apphost-lean/**`。实测发现本仓同一提交内**同时存在两个被跟踪的顶级树**：`Docs/`（30 个文件：v4.3.1 历史 ChangeLog/ReleaseNote/PDF 等）与 `docs/`（70 个文件：VitePress 站点、web-api 文档）。macOS APFS 大小写不敏感工作区二者折叠为同一物理目录，继续使用大写 `Docs/` 会与现存 `docs/` 树发生 git 路径大小写冲突并把审计件混入历史发布文档树。因此**规范化为小写 `docs/audits/apphost-lean/**`**（本目录）。此为对合同字面的唯一偏离，已在交付评论中披露。复核命令：
`git ls-tree HEAD Docs docs`

## 交付文件（AC 映射）

| 文件 | AC | 内容 |
|---|---|---|
| `dependency-closure.md` | AC1 | 双仓第一方消费者闭合：每个判断 = 路径 + 精确 SHA + 可重放扫描命令 + 排除项 + UNKNOWN |
| `migration-matrix.csv` | AC2 | 机器可读迁移矩阵（old_path→proposed_new_path→…，四分类，最小可回退分组） |
| `key-risks.md` | AC3 | 关键风险复核 + 冻结语义清单（只记录，不提议更改） |
| `verification-boundary.md` | AC4 | rename-only / 解耦 / HaLow 容量 / 实时性能最低验证矩阵 + 五级分类 + 实施建议 + 残余风险 |

## 方法与红线

- 证据优先：所有结论来自 `git grep` / `rg -n` / `git ls-files` / `git ls-tree <sha>` / Makefile 与链接脚本静态分析，命令随文可重放；不依赖记忆。
- 机械穷举 + 记录排除项；诚实 UNKNOWN 优先于猜测闭合。
- 本审计不裁决旧 ABI/Run 是否淘汰（A 独占）；不把文件移动等同于瘦身或提速；不将历史约 270 KB 整树增量当作可删除 Host 字节；不宣称 FPS 已改善。
- 容量/性能目标仅指 Wi-Fi HaLow 配置（`NETIF_WIFI_HALOW_IS_ENABLE=1`）。

## 快速复数字段（全部为源码工作树口径，非固件 footprint）

- `Custom/Services/AppHost/`：36 文件（18 `.c` + 18 `.h`），20,050 行，716,968 字节（`git ls-files Custom/Services/AppHost/ | xargs wc -l/-c`）。
- `Custom/Services/Web/api/api_app_*.c/.h`：208,518 字节（install 1001 行 / session 3497 行 / control 462 行）。
- Web 前端 App 管理相关：22 个跟踪文件，419,067 字节。
- 固件仓 `tests/` 内 App/AppHost 直接相关目录 26 个（另有 3 个相邻护栏目录），工作树 4,092 KB（含 `.neapp`/`.neui` fixture 与生成物）。
- AppHost 直编编译单元：`Appli/Makefile:686-712` 共 15 个 `.c`；另有 3 个 `.c`（`app_sidecar_verify.c`、`app_sidecar_lifecycle.c`、`app_pair_installer.c`）经 `app_installer.c:1305-1307` 以 `#include "*.c"` 并入。
- HTTP 路由：`/api/v1/app/*` 共 16 条（install/installed/run/session×3/report/sidecar×5/control×3/business）。
