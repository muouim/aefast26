#!/bin/bash

# Define the FPTree directory as a variable
dm_tree_dir="$HOME/aefast26/FPTree"

# Navigate to the build directory under FPTree
cd $dm_tree_dir/build

# Start the server in the background and log its process ID
sleep 10
nohup ./server >> log.out 2>&1 & echo $! > /tmp/server_log.out
echo "server started"

# Sleep for 100 seconds
sleep 100