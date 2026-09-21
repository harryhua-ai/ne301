# Line Counting V1 验证报告（Task 13 / Issue #16）

执行分支：`counting`（Task 2–12 已交付，见各 Issue 报告）
执行角色：B（执行方），本报告不关闭任何 Issue，待 A 侧独立审查。

## 1. 已执行验证（COMPLETED）

### 1.1 host 测试套件（plan Step 1）

`make -C tests/host test` — 8 套件全绿，覆盖 Issue #16 前 6 项 AC 的宿主可验证部分：

| 套件 | 用例数 | 覆盖场景 |
|------|--------|----------|
| nn_model_meta | 多项 | 模型元数据解析（Task 1） |
| video_ai_active_model | 多项 | reload 状态机、失败路径（Task 1） |
| video_ai_result_list | 多项 | 多消费者恰好一次分发（Task 3） |
| lc_engine | 7 | IN/OUT 往返、交替反抖、零检测退役、age 饱和、退化线、窗口快照 |
| line_counting_config | 7 组 | 默认值/校验 13 拒绝/19 字段映射/UTF-8 11 断言/NULL 安全 |
| line_counting_app | 14 | 四态状态机、代际变更保留/瞬态清理、零检测驱动引擎、事件环 50 上限、窗口结算、disable/enable、target 重置、counter_name 保留、持久化失败路径 |
| lc_delivery_queue | 16 | FIFO/enqueue-reopen、双通道独立态、count/byte 溢出逐旧、torn-write（commit 缺失/payload 截断/journal 断条/CRC 损坏）、compaction 切换、存储故障不毒化、回收无复活、NOT_REQUIRED 语义 |
| lc_report | 4 | schema-v1 全字段、ISO-8601/invalid clock null 语义、tracks/heat 可选块、缓冲不足 |

### 1.2 全量构建（plan Step 2）

- `make STEDGEAI_VARIANT=4.0 pkg` → EXIT=0，产物 `build/ne301_Wifi_flash_v2.16.5.108_pkg.bin`（含 Web 资源与 AI 模型打包）。
- `pnpm build`（Web 前端）→ 通过；`tsc --noEmit` 0 错误。
- `git diff --check` 干净。

### 1.3 Web 前端测试

`pnpm vitest run src/test/lineCounting*` → 3 套件 10 用例全过（API 路径/normalized 保存/reset 无 body；兄弟 tab/草稿不 POST/target 确认/overlay 原语与默认排除项；菜单无旧项/旧路由 Navigate 重定向）。

### 1.4 最终静态搜索（plan Step 9）

- `rg "LC_REALLOC|PC_REALLOC" Custom` → 0 命中。
- `rg "people_counting" Custom`（源码）→ 仅剩兼容/迁移边界（Task 12 报告已逐一定性）：旧 REST 适配器、迁移读取（pc NVS 键 + load/migrate 函数 + `people_counting_config_t` 迁移源类型）、`/config/pc_totals.json` 读取。
- `rg "people-counting" Web/src` → 仅重定向路由与导航测试断言。
- locale 死键清理（本轮缺陷修复）：删除 `sys.menu.people_counting` 与无消费者的 `sys.pc.*` 段（en/zh）。

## 2. 未执行验证（NOT COMPLETED — 需真机与长时间运行）

以下 plan Step 3–8 场景需要物理设备（ST-Link 烧录、摄像头实流、MQTT/Webhook 真实 broker、时钟源控制）与 ≥24 小时连续运行，本环境无法执行，**如实报告 NOT COMPLETED**：

- [ ] Step 3 设备上模型切换三案例（OD person→person / →无 person / 换回自动恢复）。
  宿主等价：`test_model_change_preserves_business_clears_transient`、`test_target_class_invalid_and_recovery` 已验证状态机语义；真机摄像头流验证未做。
- [ ] Step 4 设备上 target/reset/disable-reenable 场景。
  宿主等价：`test_target_change_resets_session`、`test_manual_reset`、`test_disable_closes_window_and_reenable_fresh`；真机未做。
- [ ] Step 5 传输持久化（断连累积→重启→逐通道恢复→payload 字节级稳定）。
  宿主等价：队列 reopen/FIFO/CRC/回收测试；真机 broker 链路未做。
- [ ] Step 6 clock-invalid 上报（无 UTC 启动）。
  宿主等价：`test_iso8601_and_invalid_clock` 序列化语义；真机 RTC 场景未做。
- [ ] Step 7 真机溢出/存储故障注入。
  宿主等价：queue count/byte 溢出、torn-write、存储故障测试；真机未做。
- [ ] Step 8 ≥24h 稳定性 soak：**NOT COMPLETED**（零 HardFault、无内存持续增长、无 wild/double free、tracker/队列有界、无重复 report 身份——均未获得真机 24h 证据）。

## 3. 结论

宿主可验证的全部测试、构建与静态搜索通过；设备运行时与 24h soak 场景 NOT COMPLETED。建议 A 侧审查代码后安排真机验收与 soak 计划。
