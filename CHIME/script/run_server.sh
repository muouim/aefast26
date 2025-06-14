#!/bin/bash

# Define the CHIME directory as a variable
dm_tree_dir="$HOME/aefast26/CHIME"

# Navigate to the build directory under CHIME
cd $dm_tree_dir/build

# Start the server in the background and log its process ID
sleep 10
nohup ./ycsb_test 7 72 8 randint a >> log.out 2>&1 & echo $! > /tmp/server_log.out
echo "server started"

# Sleep for 100 seconds
sleep 100