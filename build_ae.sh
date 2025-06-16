#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

compute_nodes="6 5 4 2 1" # including local node 7
memory_nodes="3"
current_dir="$PWD"
folder_name=$(basename "$PWD")
output_path="$current_dir/compile.output"

# Status file, placed in /tmp directory
status_file="/tmp/build_ae.flag"

# Function to calculate elapsed time
elapsed_time() {
    local start_time=$1
    local end_time=$(date +%s)
    echo "$((end_time - start_time))"
}

# Phase 0: Check script running status
echo "---------- Phase 0: Check script running status ----------"

# Output the current time when the script starts
start_time=$(date +%s)
current_time=$(date)
echo "Script started at: $current_time"

if [ -f "$status_file" ]; then
    if grep -q "running" "$status_file"; then
        echo "The script is already running, please wait."
        exit 1
    elif grep -q "completed" "$status_file"; then
        echo "Environment setup is complete, no need to run the script again."
        exit 0
    fi
else
    echo "This script has not been run before, starting the execution..."
    # Mark the script as running
    echo "running" > "$status_file"
fi

# Phase 1: Configure hugepage on compute nodes
echo "---------- Phase 1: Configure hugepage on compute nodes ----------"
for n in $compute_nodes; do
    echo "Configuring hugepage on compute node $n"
    ssh skv-node$n "/bin/bash -c 'sudo sysctl -w vm.nr_hugepages=12768'; exit"
done

# Phase 2: Copy and compile code on compute nodes
echo "---------- Phase 2: Copy and compile code on compute nodes ----------"
id=1
for n in $compute_nodes; do
    echo "Copying and compiling code on compute node $n"
    scp -r "$current_dir" skv-node$n:~/ > "$output_path" 2>&1
    ssh skv-node$n "/bin/bash -c 'cd $folder_name && python3 ./AE/configure.py --node_id $id && bash ./compile.sh > $output_path 2>&1'; exit;"
    ((id++))
done

# Phase 3: Configure hugepage on memory nodes
echo "---------- Phase 3: Configure hugepage on memory nodes ----------"
for n in $memory_nodes; do
    echo "Configuring hugepage on memory node $n"
    ssh skv-node$n "/bin/bash -c 'sudo sysctl -w vm.nr_hugepages=82768'; exit"
done

# Phase 4: Copy and compile code on memory nodes
echo "---------- Phase 4: Copy and compile code on memory nodes ----------"
for n in $memory_nodes; do
    echo "Copying and compiling code on memory node $n"
    scp -r "$current_dir" skv-node$n:~/ > "$output_path" 2>&1
    ssh skv-node$n "/bin/bash -c 'cd $folder_name && python3 ./AE/configure.py --node_id -1 && bash ./compile.sh > $output_path 2>&1'; exit;"
done

# Phase 5: Build code on local node (node7)
echo "---------- Phase 5: Build code on local node (node7) ----------"
sudo sysctl -w vm.nr_hugepages=12768
python3 ./AE/configure.py --node_id 0 && bash ./compile.sh > "$output_path" 2>&1

# Final: Mark status as completed
echo "completed" > "$status_file"

# Output the final time and total elapsed time
end_time=$(date +%s)
elapsed=$(elapsed_time $start_time)
current_time=$(date)
echo "Script completed at: $current_time, Total elapsed time: $elapsed seconds"
echo "---------- All phases completed. Output saved to $output_path ----------"
