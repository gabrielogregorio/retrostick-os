#!/bin/bash
# Watches the SD card: waits for it to LEAVE the PC (go to the stick) and COME BACK, copies the logs
# to out/logs-YYYYMMDD-HHMMSS/ and runs analyze_logs.py. Checks every 10 s.
C=${1:?usage: tools/wait_card.sh <SD card mount point>}
P=$(cd "$(dirname "$0")/.." && pwd)
echo "$(date +%T) waiting for the SD card to leave the PC ($C)"
while [ -d "$C/retro" ]; do sleep 10; done
echo "$(date +%T) SD card left; waiting for it to come back"
while [ ! -f "$C/retro/log-steps.txt" ]; do sleep 10; done
sleep 5
D=$P/out/logs-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D"
cp "$C"/retro/log-*.txt "$D"/
echo "$(date +%T) SD card is back; logs in $D"
python3 $P/tools/analyze_logs.py "$D" | tee "$D/summary.txt"
