# Docsnap 集成设计——NE302 文档触发拍照 → NeoMind 上传确认 → 柔和灯光反馈

日期：2026-11-23　**rev D（唯一权威说明；本文与代码不一致处以代码为准。旧 rev A/B/C 的状态机、ACK、恢复描述全部作废）**　授权边界：分支就绪，OTA/PR/烧录 HOLD

## 1. 复用面

| 能力 | 实现 | 用法 |
|---|---|---|
| 高清拍照 | `system_service_capture_request()`（并发守卫+OTA 忙拒；pipe1 现帧硬件 JPEG；与 AI pipe2 并行） | 直接调用；分辨率由 camera 配置决定（部署设 1080p/4MP 并真机验字迹，720p 不默认达标） |
| 持久待传 | `upload_coordinator`：meta JSON 含 image_id+trigger，PENDING→SENT、retry、后台 flush | 拍照即入队；重试/断网保留全由协调器负责 |
| MQTT 图片 | metadata(image_id=cam01_<unix>_<seq>)+device_info+ai_result+base64 | image_id = ACK 匹配键；跨重启唯一（见 §3） |
| 下行命令 | `down/control` 主题+解析器 | 新命令 `photo_ack` |
| 灯光 | 补光灯 TIM3 PWM | duty 20%（≥15% 起辉）×250ms；**恢复只走 set_brightness，零持久写** |
| 检测信号 | ai_telemetry 回调（AI 节点唯一槽）尾部 docsnap 通知 | document 类名门控；独立于 telemetry 开关 |

## 2. 状态机（rev D）

```
   ONE valid detection      photo_ack(精确 image_id)
   ARMED ────────────────► PLACED ──────────────────────► （placement 关闭）
     ▲  失败→闩锁不开         │ absent×3（纸移走）              │ absent×3
     └──────────────────── ARMED ◄──────────────────────────┘
```

- **触发**：单次有效正检出立即拍；重复检出由 placement 闩锁+拍照并发守卫防重。
- **闩锁**：拍照成功**且 image_id 非空**才进 PLACED；失败/BUSY/空 id 不开闩，下次检出自动重试。
- **逐照片独立跟踪**（上限 4 张在途）：纸移走只重开拍摄资格；在途照片不丢、乱序 ACK 各自闪一次。
- **重启恢复（P0 修正）**：启动时从协调器 PENDING 记录（`trigger=ai_doc`，`record_info_t.image_id`）播种 ACK 表；协调器 `trigger_str()` 已补 `AI_DOC→"ai_doc"` 映射；**docsnap 服务优先级 12（在协调器 11 之后）+ 显式 depends_on**。SENT（已传但 ACK 丢）不补闪——已知限制。
- **已知固件缺陷（本线修复）**：stop→start 遗留 STOP 事件标志会杀新 worker——start() 清 flags。

## 3. ACK 契约

- `{"cmd":"photo_ack","image_id":"cam01_<unix>_<seq>"}` 于 `ne302/<mac>/down/control`；**image_id 必填**：无 id/未知 id/重复 id 一律拒绝（日志区分 duplicate 与 unknown）。
- **id 跨重启唯一（P0 修正）**：NVS 持久单调计数器（`img_seq`）——时钟重置/回拨不再产生历史 ID；同一次生成进 MQTT payload 与协调器 meta。
- **ACK 邮箱（P1 修正）**：8 槽 FIFO + 同 id 入队去重——4 张并发上限内有效确认不丢；满队丢新 ACK 为响亮兜底（正常不可达）。
- 验证：**本地 MQTT 收图 + 注入 ack**（`docsnap/e2e_local_mqtt/`，mosquitto）；**最终产品 DONE 仍须真实 NeoMind 接收后返回确认证明（模拟 ACK 不可替代）**。生产 NeoMind 路径：图片落盘成功→URL metric→规则引擎→data-push 发 ack（源码级证据，`ne302_camera.json` 已映射 metadata.image_id）。

## 4. 灯光

duty 20%/250ms 单次为现场起点；save→override→blink→settle 400ms→`set_brightness(saved)` 运行时恢复；不破坏补光状态；"不刺眼"以实机体验为准；不用 PG9 指示灯。

## 5. 提交纯度（rev D 整理）

- **保留的运行依赖**：DocPresence FP32 触发链（pp_doc_presence/nn.c f32 转换/曝光归一化 video_ai_node——实况检出实测依赖）、`nn_get_model_output_buffer`（上游已有）。
- **已剥离的纯取证**：main.c XSPI 故障 dump、api_model_validation diag 接口扩展（文件还原 upstream）、ai_service 输入 CRC/dump、nn.c/nn.h nn_diag_t 统计块与 `nn_inference_frame_diag`（消费链全删，零残留）。
- 338 部署基线中的 f32 转换、标量安全（via json output_spec 的尺寸校验路径）等运行功能保留。

## 6. 验收映射（A 七场景）

放入→拍一张（§2）；传输正常→NeoMind 收图+可核对确认（QoS1 PUBACK+落盘可见+ack）；确认→柔闪一次（§3）；文档不动→不重拍（闩锁）；失败/断网→保留待重传不误报（协调器+无 ack 不闪）；移走重放→新轮次（absent×3）；NeoMind 后续处理失败不影响传输确认（ack=落盘事件）。

## 7. 如实声明

①触发阈值=运行阈值（NVS 现值 0.30）——真实空白纸基线（blankpaper_baseline 七步）仍是端到端验收内容；②响应秒级（FP32 3.5s 节拍，性能优化已停）；③≥1MB 分块与 NeoMind base64 解码未核验（1080p/4MP 下可能触发——真机验）；④SENT 不补闪；⑤数据Report QoS 必须=1，上传模式禁止 LOCAL_ONLY/METADATA_ONLY（degraded 门控+ERROR 日志）。
