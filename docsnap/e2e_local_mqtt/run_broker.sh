#!/bin/bash
# Local MQTT broker for docsnap e2e (port 1883, anonymous)
cd "$(dirname "$0")"
pkill -f "mosquitto -c" 2>/dev/null
printf "listener 1883 0.0.0.0\nallow_anonymous true\nlog_dest file %s/broker.log\nlog_type all\n" "$(pwd)" > mosquitto.conf
mosquitto -c mosquitto.conf -d
sleep 1
echo "broker up (pid $(pgrep -f 'mosquitto -c'))"
