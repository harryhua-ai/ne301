# Docsnap 集成设计——NE302 文档触发拍照 → NeoMind 上传确认 → 柔和灯光反馈

日期：2026-11-23　**rev C（对齐 App 342 实现与 A REQUEST_CHANGES 整改；本文为唯一协议说明，旧 rev A/B 的状态机与 ACK 描述作废）**　授权边界：本地实现+构建，**未授权烧录/发布**

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

## 2. Docsnap 状态机（rev C）

```
   ONE valid detection      photo_ack(精确 image_id)
   ARMED ────────────────► PLACED ──────────────────────► （placement 关闭）
     ▲  capture_request()     │                              │
     │   失败→闩锁不开，        │ absent×3（纸移走）             │ absent×3
     │   下次检出自动重试        ▼                              ▼
     └──────────────────── ARMED ◄──────────────────────────┘
```

- **触发**：单次有效正检出立即触发（无连续帧要求）。重复检出由 placement 闩锁 + 拍照并发守卫防重。
- **placement 闩锁**：拍照**成功且 image_id 非空**才进入 PLACED（一次放置只拍一张）；拍照失败（BUSY/OTA/相机错/空 id）闩锁不开，下一次正检出自动重试。
- **逐照片独立跟踪**：每张照片以 image_id 独立登记（上限 4 张在途）。纸移走（absent×3）只重开拍摄资格，**绝不丢弃在途照片**；乱序 ACK 各自独立确认、各自闪一次灯。
- **重启恢复（rev C 新增）**：未确认照片的 image_id 已由 upload_coordinator 持久化（meta JSON）。服务启动时从 PENDING 且 trigger=ai_doc 的记录播种 ACK 跟踪表（复用持久标识，不建第二套持久队列）——重启后补到的 ACK 仍然闪灯。SENT 状态（已上传但 ACK 丢失）不补闪，如实声明。
- **ACK 邮箱（rev C 修复）**：单槽 mailbox 会覆盖未处理 ACK → 改为 4 槽 FIFO；满队丢弃新到 ACK 并 WARN（4 张在途上限下满队=worker 卡死，日志可见）。

## 3. ACK 契约（rev C：强制 image_id 绑定）

- 固件订阅现有 `ne302/<mac>/down/control`；命令：`{"cmd":"photo_ack","image_id":"cam01_..."}`。
- **image_id 为必填**：无 id / 未知 id / 已确认 id（重复 ACK）一律拒绝并记日志，不接受为成功。
- **id 唯一性（rev C 修复）**：`mqtt_service_generate_image_id` 追加每开机单调序号（`cam01_<unix>_<seq>`）——同秒拍摄/时钟回拨/重启不再碰撞；该 id 同时写入 MQTT payload 与协调器 meta JSON（同一次生成，天然一致）。
- **验证目标（Harry 指示 2026-11-23）：NeoMind 应用侧不做验证关卡——以"本地 MQTT broker 收到图片上行"为准**，photo_ack 由测试侧注入（`docsnap/e2e_local_mqtt/`，mosquitto）。生产接 NeoMind 时：规则引擎监听图片入库事件 → data-push MQTT 向设备 `down/control` 发 photo_ack（源码证据：base64→`save_image_binary` 落盘成功→URL metric→规则引擎→data-push，即 ACK 发生于完整接收之后）。NeoMind 侧 `ne302_camera.json` 已定义 `metadata.image_id`/`image_data` 字段；**规则对同一消息 image_id 的精确关联仍属生产部署配置项，不作为固件侧验证前置**。

## 4. 灯光语义（rev C：运行时恢复，无持久写）

- 补光灯 PWM duty **20%（可配）**、单次 **250ms**、`settle 400ms` 后恢复——满足"柔和、单次、低亮度"。
- **恢复只走 `set_brightness`（运行时 RAM+占空比），不调 `set_config`（会持久化 NVS）**——每次成功提示零持久写（A fix 3）。恢复值 = 闪灯前 `get_config` 读到的 brightness_level。
- 不用 PG9 系统指示灯（SYSTEM_INDICATOR 状态机语义冲突）。20% 占空比的"不刺眼"仍以实机体验为准。

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

## 8. 实现结果（rev C，2026-11-23）

- 分支：`feat/docsnap-neomind`（基线 = `camthink-ai/ne302` main `d153d5f2`，已核实上游不含 DocPresence 部署——触发源链路为本分支必需增量，分两个逻辑提交明确区分：①DocPresence FP32 触发基线 ②docsnap 功能）。此前 `harryhua-ai/ne301` 的 `848b01b1` 仅作迁移来源，不作交付分支。
- 制件：`ne302_App_signed_v4.3.1.342_pkg.bin` SHA 见提交报告（构建成功、宿主模拟 ALL PASS）。
- rev C 整改清单（A REQUEST_CHANGES 五项）：①ACK 单槽 mailbox→4 槽 FIFO（连续双 ACK 测试通过）②空 image_id 拒绝入跟踪 + generate_image_id 加序号唯一化 ③灯光恢复只走 set_brightness（零持久写）+ settle 400ms ④上传模式前置校验：LOCAL_ONLY / METADATA_ONLY → degraded 标记 + 拍照门控（启动时 ERROR 日志）⑤重启恢复：从协调器 PENDING+ai_doc 记录播种 ACK 表（record_info_t 新增 image_id 字段，来自已有 meta JSON，零第二持久队列）。
- 已知限制（如实）：SENT 状态（已上传但 ACK 丢失）重启后不补闪；灯光 AUTO 模式下 capture 路径的补光行为与提示灯正交，实机联调观察。
- 部署前置：本地/生产 MQTT broker 可达 + 设备 MQTT 指向之 + `data_report_qos=1` + 上传模式非 LOCAL_ONLY/METADATA_ONLY。验证无需 NeoMind 应用侧参与（本地 mosquitto 收图 + 注入 ack，`docsnap/e2e_local_mqtt/`）。
