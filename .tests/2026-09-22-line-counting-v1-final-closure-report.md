# Line Counting V1 最终验收/收口报告

日期：2026-09-22 夜 · 基线：d4aed88a → 收口 HEAD：71ad0ddf（文档）/ 5b1c270e（代码）· 全部已推送 origin/counting
设备：192.168.93.111 · 运行版本：App v4.3.1.354（异步 reset）+ Web 1.5.0.7（index-DF1JSWu4.js，与 HEAD 一致）

---

## 1. d4aed88a → HEAD 逐 commit 自审（7 个，无冻结语义改动）

| Commit | 类型 | 真实故障 | production call chain | 文件 | Issue/AC | 测试/真机证据 |
|---|---|---|---|---|---|---|
| 40f43d20 | docs | 「保存后刷新线丢失」反复报告 | 只读取证（无代码） | .tests 根因文档 | #18 附属 | 只读比对：渲染=服务器逐位一致 |
| 5a77489a | docs | 修复"不生效"假象 | 无代码 | 同上 | #18 附属 | 实测 `max-age=86400` + SPA fallback 200 |
| 384ea715 | fix(web) | 页面装载撞 401/超时波后 getConfig 单次失败 → config 永久 null → 整页死壳（无线/不能画/不能存） | mount → pollRuntime(2s) → getConfig 失败不再恢复 | line-counting-module.tsx + test | #18 AC（页面必须可用） | 单测：连续失败后轮询恢复；真机：死壳→刷新恢复实证 |
| 05f1a447 | fix(web) | 用户重定义交互：完成绘制不保存导致"刷新线丢"；重置无法清线 | toolbar → onFinishDraw → handleSave（整份 draft 单次 normalized POST）；重置=清空 draft.line | module/LineToolbar/locales/tests | #18 AC + Harry 指令 | 真机：完成并保存一键落盘；重置后预览清空、保存禁用 |
| 11523a73 | fix(web) | 重置统计 9.6s 无反馈像死机 | 确认重置 → resetBusy 遮罩 → reset() → 轮询 | module/locales/tests | #16 UX + Harry 报告 | 单测：遮罩出现/消失 |
| 5b1c270e | feat(fw+web) | POST /reset 同步阻塞 web 任务 9.61s（并发探测 9.46s 实证） | POST /reset → line_counting_reset() 置请求标志立即返回 → lc_tick_task 执行 lc_app_reset（事务序列原样）→ /status `resetting` 字段 → 前端轮询 | api_line_counting_module.c / line_counting.c/.h / 前端 / test_lc_rest_contract | #16 性能（Harry 选方案 3） | 实测 POST 0.01s（原 9.61s）、统计 ~1s 异步清零、host 14 套件 PASS |
| 71ad0ddf | docs | 设备路由表运行期丢失单条路由事件 | 无代码（只读取证+重启恢复） | .tests 文档 | 独立固件 Issue 素材 | 106/106 NOT_FOUND + 全量 21 GET 路由映射 + 重启恢复 |

**冻结语义核查（d4aed88a→HEAD 固件 diff 全量审阅）**：
- `lc_app_reset` 事务序列（txn_prepare → totals 清零持久化 epoch++ → queue_clear → commit_reset_ram → txn_clear）**逐行未动**；
- `lc_app_apply_config`、`lc_delivery_queue_*`、totals/txn 存储实现、transport（MQTT/Webhook drain）、target/counter/model-switch 语义 **零 diff**；
- 唯一行为变化 = reset 的**执行上下文**（web 任务 → lc_tick_task）与 API 时序（立即返回 + resetting 轮询），事务原子性/掉电安全不变；REST 契约测试已同步并通过。

## 2. #18–#23 证据收口

| Issue | 结论 | 证据（全部真机/当前构建） | 缺口 |
|---|---|---|---|
| #18 三页重构 | **PASS**（按修订版 AC） | 三页签+工具栏+896px 卡宽+Video 吃满内容区（IAB 实测 cardWidth=896）；draft 跨页保留；Reset Line 预览清空+保存禁用；唯一 Save（参数配置/高级设置各一处同一实例）；事件四列真实 target class；模型 name-only；MQTT/Webhook 一级 Tab 无回归 | A 独立复核（AC 修订见 issue 评论 5774441777） |
| #19 i18n | **PASS（保持）** | LC 全组件走 lingui catalog；真机 zh 渲染逐项核对 | 无 |
| #20/#21 按钮主题 | **PASS（保持）** | 主按钮白字对比度修复未回退 | 无 |
| #22 坐标语义 | **PASS（保持）** | normalized↔permille 转换未改动；今日多次真机 POST/GET 往返字节级一致 | 无 |
| #23 模型 metadata | **PASS（保持）** | 「当前模型：Person Tiny (YOLO11n) Object Detection Model」无版本号（今日真机截图+DOM） | 无 |

**#18–#23 期间新发现的设备级缺陷（独立 Issue 素材，均不在 #18–#23 范围内擅改）**：
1. 固件：web 路由表运行期丢失单条表项（今日 /config NOT_FOUND 事件，重启恢复，soak 已布告警）；
2. 固件：`GET /config` 等在 401/超时波窗口（实测 60s+）间歇失败——保存可靠性的根；
3. 固件：静态服务 `index.html` 下发 `max-age=86400` → web OTA 后浏览器最长 24h 运行旧版本（需强刷）；
4. 固件：LC 配置不接受无线状态（`invalid_config`）→ 重置只能是"恢复已保存线"（当前语义）。

## 3. #16 主线 checklist（soak 起止/异常统计）

**Soak 窗口**：2026-09-22 12:17:31 起持续计时 → 21:01 已运行 **8h44m**（≥24h 目标剩余 ~15.3h，**不重新计时**）。
**异常统计**：有效采样 482+；HTTP000/掉线 13 行（=5 次 web 重烧 + 1 次 App A/B 升级重启，全部计划内并有 MARKER/自动记录）；ALARM（路由丢失复发）**0**；NOT_FOUND（采样内）**0**；HardFault 指示 **0**（无串口，代理证据=全程可响应、计划外重启 0）。

| #16 AC | 状态 | 证据/缺口 |
|---|---|---|
| IN/OUT/round-trip/anti-bounce/zero-detection/retirement | 部分覆盖 | IN/OUT/事件流真机今日持续验证（events/totals 实时增长）；round-trip=MQTT backlog 排空观测；anti-bounce/zero-detection/retirement=host 套件覆盖，**真机专项场景缺** |
| Compatible/incompatible model switch-back | 部分覆盖 | 早前轮次（v4.3.1.33x）CLI model reload 真机做过；**当前 354 构建未重测** |
| Target change / reset / disable-reenable | 部分覆盖 | target change 与 reset 今日真机多次（含异步 reset）；disable-reenable=host 覆盖，真机专项缺 |
| MQTT/Webhook 独立失败/重试 | 部分覆盖 | 队列 backlog/dropped 计数真机可见（排空正常）；独立故障注入缺 |
| Queue grow/drain/overflow/reboot/torn-write/corruption | 部分 | host 故障注入矩阵 PASS；真机 reboot 恢复今日多次实证（totals 跨重启保持）；torn-write/corruption 真机缺 |
| Invalid UTC / storage failure | 部分 | RTC invalid（1970）启动态曾观测；storage failure=host 注入；真机专项缺 |
| ≥24h soak 零 HardFault/内存增长/野指针/无界增长 | **进行中** | 已 8h44m 无 HardFault 指示、无计划外重启；内存增长无遥测（缺 heap 采样）；剩 ~15.3h |
| Final full build/test/diff clean | **PASS** | 固件 host 14 套件 + make app/pkg-app（签名 v4.3.1.354）+ web 79/79 + build + diff-check 全绿 |

## 4. #1（客流预览 HardFault）AC 覆盖映射 —— 只映射，不开发

| AC | 现有证据 | 缺口 |
|---|---|---|
| AC1 持续预览 30min+ 无黑屏/HardFault | people-counting 分支 290 soak 曾验证；counting 分支旧 pc 路径已移除（A 评论：superseded/PASS with device-only residual）；今日预览长时间运行无黑屏 | **缺**：当前构建的专项 30min 连续预览受控观测 |
| AC2 检测/跟踪/IN/OUT/预览持续正常 | 今日真机：事件持续产生（sequence 70+）、totals 持续增长、预览流 readyState=4 | 无 |
| AC3 刷新/重进/流重建无泄漏 | 今日刷新/重进数十次，功能正常；视频流多次重建 | **缺**：内存/句柄泄漏的量化遥测 |
| AC4 HardFault 符号化+根因+修复 | 已完成（UNALIGNED UsageFault → frame-buffer realloc no-copy + timer stack 溢出，两处针对性修复，PR #3） | 无 |

**结论**：#1 的 AC4 证据完整；AC2 基本覆盖；AC1/AC3 需一次受控的 30min 连续预览观测 + 内存遥测才能正式宣称覆盖。不建议在 #16 soak 完成前单独重测（可合并进 soak 收尾观测）。

## 5. 工程门
- 固件：make app / pkg-app（签名 v4.3.1.354）PASS；host 14 套件 PASS（含更新后的 REST 契约）。
- Web：79/79 vitest、tsc+vite build PASS。
- git diff --check 干净；工作树干净；全部推送 origin/counting。
- 本轮（收口报告）**零代码改动**。
