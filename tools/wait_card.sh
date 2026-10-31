#!/bin/bash
# Watches the SD card: waits for it to LEAVE the PC (go to the stick) and COME BACK, copies the logs
# to out/logs-YYYYMMDD-HHMMSS/ and runs analyze_logs.py. Checks every 10 s.
card=${1:?usage: tools/wait_card.sh <SD card mount point>}
project=$(cd "$(dirname "$0")/.." && pwd)
poll_secs=10
settle_secs=5

echo "$(date +%T) waiting for the SD card to leave the PC ($card)"
while [ -d "$card/retro" ]; do
    sleep "$poll_secs"
done
echo "$(date +%T) SD card left; waiting for it to come back"
while [ ! -f "$card/retro/log-steps.txt" ]; do
    sleep "$poll_secs"
done
sleep "$settle_secs"
log_dir=$project/out/logs-$(date +%Y%m%d-%H%M%S)
mkdir -p "$log_dir"
cp "$card"/retro/log-*.txt "$log_dir"/
echo "$(date +%T) SD card is back; logs in $log_dir"
python3 "$project/tools/analyze_logs.py" "$log_dir" | tee "$log_dir/summary.txt"
