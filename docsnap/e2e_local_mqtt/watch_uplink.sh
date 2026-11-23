#!/bin/bash
# Watch device uplink: ne302/# (report photos + telemetry + control responses)
cd "$(dirname "$0")"
echo "subscribing ne302/# -> uplink.log (ctrl-c to stop)"
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'ne302/#' -v | tee uplink.log
