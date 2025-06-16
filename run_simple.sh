#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

baselines="DMTree FPTree Sherman SMART ROLEX CHIME dLSM"
base_dir="$PWD"
status_file="/tmp/simple_exp.flag"
output_path="$base_dir/simple.output"

# Function to calculate elapsed time and format it
elapsed_time() {
    local start_time=$1
    local end_time=$(date +%s)
    local diff=$((end_time - start_time))

    # Convert to minutes or hours
    if [ $diff -ge 3600 ]; then
        # More than 1 hour, show in hours
        echo "scale=2; $diff / 3600" | bc
    else
        # Less than 1 hour, show in minutes
        echo "scale=2; $diff / 60" | bc
    fi
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

    # Output the current time and elapsed time after each project
    current_time=$(date)
    raw_diff=$(( $(date +%s) - start_time ))
    if [ $raw_diff -ge 3600 ]; then
        elapsed=$(echo "scale=2; $raw_diff / 3600" | bc)
        unit="hours"
    else
        elapsed=$(echo "scale=2; $raw_diff / 60" | bc)
        unit="minutes"
    fi
    echo "$project's results are output to $output_path"
    echo "Time after $project: $current_time, Elapsed time: $elapsed $unit"

    python3 $base_dir/AE/print_exp0.py
done

# Phase 2: Execute Python script to organize and output the results
echo "---------- Phase 2: Organize and output all the results ----------"

python3 $base_dir/AE/print_exp0.py

# Mark script as completed
echo "completed" > "$status_file"

# Output the final time and total elapsed time
end_time=$(date +%s)
diff=$((end_time - start_time))
current_time=$(date)
if [ $diff -ge 3600 ]; then
    elapsed=$(echo "scale=2; $diff / 3600" | bc)
    echo "Script completed at: $current_time, Total elapsed time: $elapsed hours"
else
    elapsed=$(echo "scale=2; $diff / 60" | bc)
    echo "Script completed at: $current_time, Total elapsed time: $elapsed minutes"
fi
echo "---------- All phases completed. Output saved to $output_path ----------"
