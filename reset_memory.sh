#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

remote_nodes="1 2 3 4 5 6"
target_user="aefast26"
mem_threshold_kb=$((1024 * 512))  # 512MB
log_file="drop_cache_kill.log"

echo "========== Starting memory cleanup ==========" | tee $log_file

for n in $remote_nodes; do
    echo "========== [skv-node$n] ==========" | tee -a $log_file

    ssh skv-node$n "bash -s" <<EOF | tee -a $log_file
echo "--- Before cleanup ---"
ps -u $target_user -eo pid,rss,comm --sort=-rss | \
    awk '{ printf "%s\t%.2f MB\t%s\n", \$1, \$2/1024, \$3 }' | head -n 10

for pid in \$(ps -u $target_user -o pid= | xargs -n1 -I{} bash -c '
    rss=\$(awk "/VmRSS/ {print \\\$2}" /proc/{}/status 2>/dev/null)
    if [ ! -z "\$rss" ] && [ "\$rss" -gt $mem_threshold_kb ]; then echo {}; fi
'); do
    echo "Killing PID \$pid (VmRSS > 512MB)"
    sudo kill -9 \$pid
done

sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

echo "--- After cleanup ---"
ps -u $target_user -eo pid,rss,comm --sort=-rss | \
    awk '{ printf "%s\t%.2f MB\t%s\n", \$1, \$2/1024, \$3 }' | head -n 10
EOF

done

echo "========== [node7 - local] ==========" | tee -a $log_file

echo "--- Before cleanup ---" | tee -a $log_file
ps -u $target_user -eo pid,rss,comm --sort=-rss | \
    awk '{ printf "%s\t%.2f MB\t%s\n", $1, $2/1024, $3 }' | head -n 10 | tee -a $log_file

for pid in $(ps -u $target_user -o pid= | xargs -n1 -I{} bash -c '
    rss=$(awk "/VmRSS/ {print \$2}" /proc/{}/status 2>/dev/null)
    if [ ! -z "$rss" ] && [ "$rss" -gt '"$mem_threshold_kb"' ]; then echo {}; fi
'); do
    echo "Killing PID $pid (VmRSS > 512MB)" | tee -a $log_file
    sudo kill -9 $pid
done

sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

echo "--- After cleanup ---" | tee -a $log_file
ps -u $target_user -eo pid,rss,comm --sort=-rss | \
    awk '{ printf "%s\t%.2f MB\t%s\n", $1, $2/1024, $3 }' | head -n 10 | tee -a $log_file

echo "========== Cleanup complete ==========" | tee -a $log_file
