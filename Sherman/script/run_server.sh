#!/bin/bash

# Define the Sherman directory as a variable
dm_tree_dir="$HOME/aefast26/Sherman"

# Navigate to the build directory under Sherman
cd $dm_tree_dir/build

# Start the server in the background and log its process ID
sleep 10
nohup ./benchmark 7 100 1 >> log.out 2>&1 & echo $! > /tmp/server_log.out
echo "server started"

# Sleep for 100 seconds
sleep 100