#!/bin/bash
# unlock_cluster.sh — Release exclusive AEC lock if held

LOCK_FILE="/tmp/aec_cluster.lock"
ID_FILE="/tmp/current_aec_id"

if [ ! -f "$ID_FILE" ]; then
    echo "[ERROR] No local AEC session ID found. Did you acquire the lock?"
    exit 1
fi

MY_NAME=$(cat "$ID_FILE")
CURRENT_NAME=$(cat "$LOCK_FILE" 2>/dev/null)

if [ "$MY_NAME" == "$CURRENT_NAME" ]; then
    rm -f "$LOCK_FILE"
    echo "[$MY_NAME] Lock released. Node is now free for others."
    rm -f "$ID_FILE"
    exit 0
else
    echo "[ERROR] Lock is not held by this session. Cannot release."
    echo "Current lock holder: [$CURRENT_NAME], your name: [$MY_NAME]"
    exit 1
fi
