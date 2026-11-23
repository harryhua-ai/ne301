# Docsnap 受控真机验收手册（A GO：验收准备；OTA/烧录/PR 仍 HOLD）

日期：2026-11-23　候选：App **4.3.1.345** = `108c71e156811f66f64b04fb454537cdbdec00933745baaa7b5868847534c870`（源码定向复审 PASS；SHA 待独立核验）
设备：NE302 `192.168.10.10`（Web 凭据见密码管理器，勿入库；MAC `04:ec:a9:20:00:6c` → MQTT 主题 id **`20006C`**）
宿主：Mac `192.168.10.100`（brew mosquitto ✓；串口 usbserial-14130@115200 监听中）

## 0. 执行前置（OTA 授权后按序）

| # | 项 | 现状（11-23 实测） | 动作 |
|---|---|---|---|
| 1 | 固件 | 当前 APP 槽身份**须在 OTA 前用 OTA export 核实**（不得凭 DocPresence 在跑推断 338；`software_version` 4.3.1.329 为编译串残留不可作依据） | export 对比 SHA → 确认现网制件与回退制件后，OTA `ne302_App_signed_v4.3.1.345_pkg.bin`；重启；`fw_version` 自报 345 + BootSuccess 计数 |
| 2 | FP32 回归 | — | 345 启动后 DocPresence 加载、`docp:` logit 正常、RTSP/预览可用（345 对 338 的检测路径差异=仅 ai_service 回调头部一行通知，理论零影响，仍须实测） |
| 3 | camera 分辨率 | **已配置 2688×1520@30（4MP）✓** | 无需改；T3 直接验收 4MP 清晰度 |
| 4 | MQTT broker | `mqtt.example.com`（占位，未连接） | POST `/api/v1/apps/mqtt/config` 指向 `192.168.10.100:1883` + `/connect` |
| 5 | QoS | 需核对 `data_report_qos` | 必须 =1（GET config 确认；QoS0 无 PUBACK 事件） |
| 6 | 上传模式 | `mode=instant, protocol=mqtt, retry=on ✓` | 保持；禁 LOCAL_ONLY/METADATA_ONLY（345 启动日志会 ERROR 告警） |
| 7 | 本地 broker | — | `docsnap/e2e_local_mqtt/run_broker.sh` + `watch_uplink.sh` |
| 8 | 串口监听 | 常驻 | `fw_version`/`docp:`/`docsnap:` 日志为行为判据 |

**顺序纪律**：345 ACTIVE（BootSuccess≥3）之前不配置任何 MQTT 变更；若需回退，APP 槽表自动回 338。

## T1 功能端到端（本地 MQTT，模拟 ACK 层）

1. 放传单/A4 于镜头前 → 串口 `docsnap: firing capture` → `captured id=cam01_... -> PLACED`
2. uplink.log 出现 `ne302/20006C/upload/report` 消息，`metadata.image_id` 与串口一致
3. `./ack.sh <image_id>` → 串口 `photo_ack confirmed` + `soft blink sent (duty=20% on=250ms)` + **补光灯肉眼可见柔闪一次**
4. 纸持续放置 ≥30s：无第二次 capture（闩锁）
5. 移走纸 → absent×3 → `paper removed -> ARMED`；重放 → 新一轮 capture（新 image_id）
6. 负向：`./ack.sh`（无 id）→ `rejected`；重复同 id → `duplicate`；随机 id → `unknown`；均无闪灯

## T2 真实空白纸（验收内容之一，规程已有）

按 `docdetect/results/blankpaper_baseline/PROCEDURE.md` 七步执行（control_flyer / blank_a4 / tilt / far / white_desk / dark_desk / empty）；本验收关注：真实空白 A4 触发拍照（检出）且 white_desk/empty 不误触发。双阈值 conf 序列留档。

## T3 高清字迹清晰度

- camera 已 4MP：拍照后从 uplink 保存 JPEG，核对：分辨率元数据 2688×1520、面单文字/条码可辨认（人眼判定 + 必要时放大截图留档）
- 若 4MP base64 消息 >2MB 或传输异常：记录实测大小；NeoMind 上限 2MB（1108p 之外此为回退依据，不自动降级）

## T4 断网/重启恢复

- 断网：**先停 broker（`pkill mosquitto`），再放纸触发拍照**——确保照片在上传成功前就断网，进入真正的 PENDING 重传路径（先拍照后断网可能测不到 PENDING）→ 报告持续 retry 日志、无闪灯 → 重启 broker → 自动重传上行 → ack → 闪灯
- 重启恢复：pending 照片存在时设备断电重启 → 345 init 播种（串口 `seeded N unconfirmed photo(s)`）→ broker 恢复重传成功 → 注入该 image_id 的 ack → **闪灯**（rev E/F 恢复语义的真机证明）
- 已知容量：恢复扫描上限 32 条 PENDING

## T5 真实 NeoMind（最终 DONE 判据，模拟 ACK 不可替代）

前置：NeoMind 实例地址 + 设备接入（`ne302_camera.json` 设备类型已含 metadata.image_id/image_data）。
1. 设备 MQTT 指向 NeoMind broker → 放单 → NeoMind UI 图片列表出现完整照片（人工核对 image_id/时间/清晰度）
2. 配置规则：图片入库事件 → data-push MQTT → `ne302/20006C/down/control` 发 `{"cmd":"photo_ack","image_id":"<event image_id>"}`（规则上下文取自同消息 metadata.image_id）
3. 放单 → **设备柔闪一次**（无需人工注入）→ 移走 → 重放再触发
4. DONE 定义核对：放入→自动拍→NeoMind 确认→柔闪→拿走可继续

## 判定汇总表（对应 A 七场景）

| 场景 | 判据 |
|---|---|
| 放入纸张/面单自动拍一张 | T1.1/T2/T5.3 capture 日志+uplink |
| NeoMind 收到完整图+可核对确认 | T5.1 UI 可见 + image_id 一致 |
| 确认后柔闪一次 | T1.3 / T5.3 串口+肉眼 |
| 文档不动不重复拍 | T1.4 闩锁 |
| 失败/断网保留待重传不误报 | T4 前半 |
| 移走重放新轮次 | T1.5 |
| NeoMind 后续处理失败不影响确认 | T5 架构性（ack=落盘事件）+T4 |

## 事故边界（已内置的自动保护）

- INT8 三败→自动回 AI_2 FP32；App 三败→回上一 APP 槽；BOOT 拨码物理兜底
- 回退条件核验（OTA 前执行并留档）：当前 APP 槽 export SHA 与本地制件对比；回退制件 337 rebuilt=`2b6ca0be…`、338=`cdaeafc2…` 均在 `build/artifacts_docpresence/`
- 设备 Web 出厂默认密码公开于官方上游（auth_mgr.h）：**验收前由 Harry 更换设备密码**（本仓库文档已全部脱敏）
