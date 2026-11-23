# Docsnap 集成设计——NE302 文档触发拍照 → NeoMind 上传确认 → 柔和灯光反馈

日期：2026-11-23　状态：设计定稿（实现见同目录 diff）　授权边界：本地实现+构建，**未授权烧录/发布**

## 0. 产品语义（A 定义，DONE 判据）

放入纸/面单 → 自动拍一张高清 → NeoMind 确认收到 → 柔闪一次 → 拿走后可继续下一张。
**闪灯 = NeoMind 已确认收到照片，不是快门已触发。**

## 1. 复用面（零重写清单）

| 能力 | 现有实现 | Docsnap 用法 |
|---|---|---|
| 高清拍照 | `system_service_capture_request()`（system_service.h:691；并发守卫+OTA 忙拒；trigger 标签） | 直接调用；pipe1 现帧 1280×720→硬件 JPEG q60，与 AI pipe2 并行不互斥 |
| 持久待传 | `upload_coordinator`：每张先落盘 `/captures/{meta,data,index}`，PENDING→SENT 状态机、retry_count、失败晋升 failed/、后台 flush（upload_task） | 拍照即入队；重试/断网保留全部由协调器负责——docsnap 不自建重传 |
| MQTT 图片 | `mqtt_service_publish_image_with_ai`：metadata{image_id=cam01_<unix>,timestamp,w,h,size,quality,trigger}+device_info+ai_result+image_data(base64 data-URL)+encoding；≥1MB 自动分块 | image_id 即 ACK 匹配键 |
| 下行命令 | 订阅 `ne302/<mac>/down/control`，解析 `{cmd,request_id}` → 分发/应答（capture/sleep/task_completed 模板） | 新增 `photo_ack` 命令，复用同主题同解析器 |
| 灯光 | 补光灯=TIM3 PWM（占空比 0-100，15% 起辉下限）；`device_service_light_set_brightness()` + `device_service_light_blink(times,interval)` | **PWM 低占空比软闪**（默认 duty 20、250ms×1），不用 PG9 系统指示灯（其语义被 set_indicator_state 状态机占用） |
| 检测信号 | ai_telemetry_result_callback（ai_service.c:1420）已持有 AI 节点唯一回调槽；pp_doc_presence conf≥阈值→is_valid=1 全幅 OD 框 | 回调尾部加一行非阻塞通知（互斥+事件位，抄 telemetry 快照模式） |

## 2. Docsnap 状态机

```
              present×K(=2)              photo_ack(匹配)
   ARMED ────────────────────► PENDING_ACK ────────────────► CONFIRMED
     ▲   capture_request()        │  柔闪一次(duty20/250ms)      │
     │        │ 失败→回 ARMED      │                              │
     │        ▼                  │ absent×M(=3)                 │ absent×M(=3)
     └────（重新武装）◄────────────┴──────────────────────────────┘
          absent 时 PENDING 的在途照片交由协调器继续重试
```

- **ARMED**：等待出现。`present` = ai 回调 `result->is_valid && od.nb_detect>0`（conf≥运行阈值，NVS 现值 0.30）。连续 K=2 次才触发（去抖 ~7s）。
- **PENDING_ACK**：已拍照（image_id 已知）。同纸持续放置**不重拍**（闩锁）。收到匹配 ACK → 柔闪一次 → CONFIRMED。absent×M → 直接回 ARMED（在途照片由协调器继续重试；下一次放入=新一轮）。
- **CONFIRMED**：已闪灯。absent×M → ARMED。**CONFIRMED 状态下即使 present 持续也不再拍**——满足"同纸不重复"。
- **ACK 匹配规则**：ack 带 `image_id` → 必须精确等于在途照片 id；ack 无 id → 仅当"恰有一张在途且纸仍在画面"时接受（本产品单张在途，时序安全）。id 不匹配的 ack 忽略（防乱序串扰）。
- **重试语义**：协调器负责重传（retry_enable/retry_max_attempts 配置）；docsnap 不重复入队、不重拍。PENDING 超时（ack_timeout=120s，仅日志观测）不触发任何动作。
- **闪灯失败**：LED ioctl 错误只记日志，不改变状态、不重拍、不重传（A 要求）。
- **重启**：状态机回到 ARMED（在途照片协调器仍在重试，但其 ACK 不会闪灯——如实声明为已知限制；后续可用协调器 index 恢复 image_id）。

## 3. ACK 契约（NeoMind 侧零代码）

- 固件订阅现有 `ne302/<mac>/down/control`；新命令：`{"cmd":"photo_ack","image_id":"cam01_..."}`（image_id 可选，见 §2 匹配规则）。
- **验证目标（Harry 指示 2026-11-23）：NeoMind 应用侧不作为验证目标——以"本地 MQTT broker 收到图片上行"为准**，photo_ack 由测试侧注入（`docsnap/e2e_local_mqtt/`，mosquitto 已装）。生产接 NeoMind 时：规则引擎监听设备图片入库事件 → data-push MQTT 向设备 `down/control` 发 photo_ack（NeoMind 源码证据：base64→`save_image_binary` 落盘成功→URL metric→规则引擎→data-push，即 ACK 发生于完整接收之后；无 HTTP 摄入端点）。
- **QoS 要求（部署硬性项）**：`data_report_qos` 必须=1（NVS 默认 0 时 QoS0 无 PUBACK 事件，协调器确认语义失效）。docsnap 初始化时检测并告警。
- 720p q60 JPEG ≈150-300KB → base64 后 ≈200-400KB < NeoMind 2MB 上限，单条消息不分块 ✓。

## 4. 灯光语义

- 补光灯 PWM duty **20%（可配）**、单次 **250ms（可配）**、间隔≥1s——满足"柔和、单次、低亮度、非满功率直射"。
- 不用 PG9 系统指示灯（SYSTEM_INDICATOR 状态机语义冲突）。
- 补光灯工作模式（AUTO/CUSTOM/OFF 配置）与 docsnap 闪灯正交：docsnap 显式 set_brightness+blink，完成后不留灯。

## 5. 代码触点（最小 footprint）

| 文件 | 改动 |
|---|---|
| `Custom/Services/Docsnap/docsnap_service.{h,c}` | **新增** ~340 行：状态机+worker 线程（事件驱动）+拍照调用+ACK 匹配+LED |
| `Custom/Services/MQTT/mqtt_service.{h,c}` | +`MQTT_CMD_PHOTO_ACK`（解析 `{"cmd":"photo_ack","image_id"}`，分发 `docsnap_service_notify_ack()`，无应答消息）；trigger 串映射 +"ai_doc" |
| `Custom/Common/Inc/aicam_types.h` | +`AICAM_CAPTURE_TRIGGER_AI_DOC = 8` |
| `Custom/Services/System/system_service.{h,c}` | `system_capture_response_t` +`image_id[64]`；capture 流程生成 image_id 后回填 response |
| `Custom/Services/AI/ai_service.c` | `ai_telemetry_result_callback` 尾部 +`docsnap_service_notify_ai(valid)`（非阻塞，静默开销 ~µs） |
| `Custom/Services/service_init.c` | 注册表 +docsnap 条目（auto_start，依赖 mqtt_service，非低功耗必需） |
| `Appli/Makefile` | +docsnap_service.c 一行 |

不改：upload_coordinator、camera/capture 底层、pp_doc_presence、JSON 配置框架（参数先用编译期默认 + 后续 NVS 化）。

## 6. 验收映射（A 七场景 → 机制）

| 场景 | 机制 |
|---|---|
| 放入纸张/面单→自动拍一张 | ARMED→present×2→capture_request（720p JPEG） |
| 传输正常→NeoMind 收到+可核对确认 | MQTT QoS1 PUBACK（broker 层）+ NeoMind 图片落盘可见（images 列表）+ photo_ack 下发 |
| NeoMind 确认→柔闪一次 | photo_ack 匹配 → PWM duty20/250ms×1 |
| 文档不动→不重复拍 | PENDING/CONFIRMED 闩锁，present 持续不重触发 |
| 失败/断网→保留待重传不误报 | 协调器持久化 PENDING+retry；无 ACK 不闪灯 |
| 移走重放→新一轮 | absent×3 回 ARMED；闩锁释放 |
| NeoMind 后续处理失败不影响传输确认 | ACK 由"图片入库"事件触发，与后续 OCR/业务解耦（A 范围定义） |

## 7. 如实声明（设计边界）

1. 触发阈值 = pp_doc_presence 运行阈值（NVS 现值 0.30）——**白纸域真实基线（blankpaper_baseline 规程）仍是前置验收项**：合成代理显示 FP32 @0.30 有桌面误报倾向、白纸贴线；真实域数据未采集。
2. 推理节拍 ~3.5s（FP32 CPU 执行）→ 触发响应 = K×节拍 + 拍照时间，秒级——本项目不再做性能优化（A 停止令），响应速度如实告知。
3. 重启丢 ACK 匹配（§2）；chunked（≥1MB）路径未与 NeoMind base64 解码核验（720p q60 不触发，留待真机）。
4. 真机验收依赖：NeoMind 实例可达 + broker 凭据配置 + data_report_qos=1 + 规则引擎 ACK 配置——部署清单随实现提交。

## 8. 实现结果（2026-11-23）

- 制件：`ne302_App_signed_v4.3.1.340_pkg.bin` = **`306fb9360ff905ac599724f1a20309433f6cb68fa0e9945579f2cc335fe037ab`**（3,893,824B，artifacts_docpresence/ 已归档）。构建成功零错误。
- 可审材料：`docsnap/docsnap_firmware.diff`（触点 diff 383 行；nn.h/ai_service 等文件含 338/339 时代未提交改动，docsnap 增量以 "docsnap" 注释标记）+ `docsnap/docsnap_service_full.c`（新服务全文件 ~350 行）+ 本文。
- 自审捕获并修复 1 个 bug：`osEventFlagsWait` 超时返回 `osFlagsErrorTimeout(0xFFFFFFFE)` 与 `DOCSNAP_FLAG_STOP(0x4)` 位重叠 → 每 1s tick 都会误判 STOP 使 worker 退出；已加错误位门控（`flags & 0x80000000` → continue）。
- **版本碰撞事件（如实记录）**：首次 docsnap 构建时 version.mk 仍为 339，覆盖了 `build/…339_pkg.bin`（污染件=d8b87ea5…）；已用 artifacts 归档件（949a2ff1…）还原 build/ 目录 339 制件并核验 SHA，version.mk 升至 4.3.1.340。artifacts 归档链全程未受污染。
- 部署前置（真机实验时）：本地/生产 MQTT broker 可达 + 设备 MQTT 配置指向之 + `data_report_qos=1`。验证无需 NeoMind 应用侧参与；接 NeoMind 时的规则配置同 §3。ACK 固件侧强制 image_id 绑定（无 id 拒收）——生产规则必须携带 image_id（经设备类型映射 `metadata.image_id` 暴露）。
