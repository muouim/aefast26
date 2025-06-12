#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

baselines="DMTree FPTree Sherman SMART ROLEX dLSM CHIME"

for project in $baselines; do
    # Check if the baseline directory exists, and if not, exit
    if [ ! -d "$project" ]; then
        echo "Error: Directory $project does not exist. Please check the baseline folder."
        exit 1
    fi

    cd "$project"

    # Check if the build directory exists, and if so, remove it
    if [ -d "build" ]; then
        echo "Removing existing build directory in $project"
        rm -rf build
    fi
    
    # Create a new build directory, run cmake and make
    mkdir build && cd build && cmake .. && make -j
    cd ..
    cd ..
done

echo "Finished"