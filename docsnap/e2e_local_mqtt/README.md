# Docsnap 本地 MQTT 端到端验证（Harry 指示：NeoMind 应用侧不作为验证目标，
# 以"本地 MQTT 能收到图片上行"为准；photo_ack 由测试侧注入）

前置：brew mosquitto 已装（mosquitto/mosquitto_sub/mosquitto_pub ✓）。
设备：NE302 192.168.10.10（Web 凭据经环境变量 `DOCSNAP_AUTH` 提供，格式 `user:pass`，勿入库）；Mac LAN IP：192.168.10.100。
固件：App 4.3.1.341 = 4199305b…（OTA HOLD 中，烧录需 A 批准）。

## 步骤

1. 本机起 broker：   ./run_broker.sh          （mosquitto -p 1883，日志 broker.log）
2. 配设备 MQTT 指向本机（qos=1 必须）：
   curl -u "$DOCSNAP_AUTH" http://192.168.10.10/api/v1/apps/mqtt/config     # 先看现值
   curl -u "$DOCSNAP_AUTH" -X POST .../apps/mqtt/config -d '{"broker":{"host":"192.168.10.100","port":1883},"qos":{"data_report_qos":1,"data_receive_qos":1}}'
   curl -u "$DOCSNAP_AUTH" -X POST .../apps/mqtt/connect
3. 开上行监听：      ./watch_uplink.sh        （订阅 ne302/#，落盘 uplink.log）
4. 放纸触发（AI 管线运行中，DocPresence 检出即自动拍照上传）
5. 在 uplink.log 找 "image_id" → ./ack.sh <image_id>   # 注入 photo_ack
6. 预期：串口 "docsnap: photo_ack confirmed" + "soft blink sent"；补光灯柔闪一次
7. 行为矩阵可全部本地演练：重复同 id ack（应无第二次闪）/ 无 id ack（拒）/ 未知 id（拒）/
   拍照后断 broker（mosquitto 停）→ 恢复 broker（协调器自动重传上行）

本机实测参数：设备 MAC 04:ec:a9:20:00:6c → 主题 id `20006C`
→ 上行 `ne302/20006C/upload/report`，下行 `ne302/20006C/down/control`
