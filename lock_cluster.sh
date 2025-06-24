#!/bin/bash
# lock_cluster.sh — Try to acquire exclusive AEC lock (non-blocking)

LOCK_FILE="/tmp/aec_cluster.lock"
ID_FILE="/tmp/current_aec_id"

if [ -z "$1" ]; then
    echo "[ERROR] Please specify the AEC name (e.g., reviewer A)"
    echo "Usage: $0 \"reviewer A\""
    exit 1
fi

AEC_NAME="$1"

if ( set -o noclobber; echo "$AEC_NAME" > "$LOCK_FILE" ) 2>/dev/null; then
    echo "[$AEC_NAME] Lock acquired. You now have exclusive access to this node."
    echo "$AEC_NAME" > "$ID_FILE"
    exit 0
else
    CURRENT_HOLDER=$(cat "$LOCK_FILE" 2>/dev/null)
    echo "[INFO] Node is already locked by [$CURRENT_HOLDER]. Exiting."
    exit 1
fi