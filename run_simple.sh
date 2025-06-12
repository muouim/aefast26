#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

baselines="DMTree FPTree Sherman SMART ROLEX dLSM CHIME"
base_dir="$PWD"
status_file="/tmp/simple_exp.flag"
output_path="$base_dir/simple.output"

# Phase 0: Check script running status
echo "---------- Phase 0: Check script running status ----------"

if [ -f "$status_file" ]; then
    if grep -q "running" "$status_file"; then
        echo "The script is already running, please wait."
        exit 1
    elif grep -q "completed" "$status_file"; then
        echo "Simple experiment is complete, no need to run the script again."
        exit 0
    fi
else
    echo "This script has not been run before, starting the execution..."
    # Mark the script as running
    echo "running" > "$status_file"
fi

# Phase 1: Run simple experiment for each baseline
echo "---------- Phase 1: Run simple experiment for each baseline ----------"

for project in $baselines; do
    echo "Running experiment for $project"
    script_path="$base_dir/$project/script/run_exp0.sh"

    if [ -f "$script_path" ]; then
        bash "$script_path" > "$output_path" 2>&1
        echo "Finished $project"
    else
        echo "Error: $script_path not found"
    fi
done

# Mark script as completed
echo "completed" > "$status_file"

echo "---------- All phases completed. Output saved to $output_path ----------"
