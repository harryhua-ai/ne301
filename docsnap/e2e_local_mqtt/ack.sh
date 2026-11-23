#!/bin/bash
# Inject photo_ack: ./ack.sh <image_id>   (no arg = id-less negative test)
cd "$(dirname "$0")"
# topic uses the LAST 3 MAC bytes, uppercase, no separator (mqtt_build_topics)
FULLMAC=$(curl -s -u "${DOCSNAP_AUTH:?export DOCSNAP_AUTH=user:pass}" http://192.168.10.10/api/v1/device/info 2>/dev/null | grep -o '"mac_address"[^,]*' | cut -d'"' -f4)
MAC=$(echo "$FULLMAC" | tr -d ':' | tr 'a-f' 'A-F' | tail -c 7)
TOPIC="ne302/${MAC}/down/control"
if [ -z "$1" ]; then
  PAYLOAD='{"cmd":"photo_ack"}'
else
  PAYLOAD="{\"cmd\":\"photo_ack\",\"image_id\":\"$1\"}"
fi
echo "publish -> $TOPIC : $PAYLOAD"
mosquitto_pub -h 127.0.0.1 -p 1883 -t "$TOPIC" -m "$PAYLOAD"
