#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

baselines="DMTree FPTree Sherman SMART ROLEX dLSM CHIME"
base_dir="$PWD"
status_file="/tmp/overall_exp.flag"
output_path="$base_dir/overall.output"

# Phase 0: Check script running status
echo "---------- Phase 0: Check script running status ----------"

if [ -f "$status_file" ]; then
    if grep -q "running" "$status_file"; then
        echo "The script is already running, please wait."
        exit 1
    elif grep -q "completed" "$status_file"; then
        echo "Overall experiment is complete, no need to run the script again."
        exit 0
    fi
else
    echo "This script has not been run before, starting the execution..."
    # Mark the script as running
    echo "running" > "$status_file"
fi

# Phase 1: Run overall experiment for each baseline
echo "---------- Phase 1: Run overall experiment for each baseline ----------"

for project in $baselines; do
    echo "Running experiment for $project"
    script_path="$base_dir/$project/script/run_exp11.sh"

    if [ -f "$script_path" ]; then
        bash "$script_path" > "$output_path" 2>&1
        echo "Finished $project"
    else
        echo "Error: $script_path not found"
    fi
done

# Phase 2: Execute Python script to organize and output the results
echo "---------- Phase 2: Organize and output the results ----------"

python3 $base_dir/AE/print_exp11.py

# Mark script as completed
echo "completed" > "$status_file"

echo "---------- All phases completed. Output saved to $output_path ----------"
