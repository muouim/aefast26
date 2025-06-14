#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

# Define the directory as a variable
dm_tree_dir="$HOME/aefast26/dLSM"
ae_data_dir="$HOME/aefast26/AE/Data"

workloads="ycsb-c insert-only update-only scan-only"
threads="72"
distribution="zipfian"
node="6 5 4 2 1"
memory_nodes="3"

# The 'data' directory is used to store the experiment results.
if [ ! -d "$dm_tree_dir/data" ]; then
    mkdir -p "$dm_tree_dir/data"
fi

for n in $node $memory_nodes; do
    ssh skv-node$n "mkdir -p $dm_tree_dir/data"
done

# The 'ae_data_dir' directory is used to collect the experiment results.
if [ ! -d "$ae_data_dir" ]; then
    mkdir -p "$ae_data_dir"
    echo "Created directory $ae_data_dir"
fi

for dis in $distribution; do
    for thread in $threads; do
        for file_name in $workloads; do
            echo "============================="
            echo "Starting run for $dis-$file_name with thread $thread"
            
            # Run script on memory nodes
            echo "Running server process on memory nodes"
            ssh skv-node3 "/bin/bash -c 'cd $dm_tree_dir/script && bash restart_memc.sh'; exit;"
            ssh skv-node3 "/bin/bash -c 'cd $dm_tree_dir/script && bash run_server.sh'; exit;"
            echo "============================="

            # Execute ycsbc on compute nodes
            echo "Running compute nodes"
            for n in $node; do
                sleep 2
                echo "Running ycsbc on compute node $n"
                ssh skv-node$n "cd $dm_tree_dir/build; nohup ./ycsbc $thread 4 $file_name $dis > $dm_tree_dir/data/node$n-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt 2>&1 &"
            done
            
            sleep 2
            echo "Running ycsbc on compute node 7"
            cd $dm_tree_dir/build && ./ycsbc $thread 4 $file_name $dis > $dm_tree_dir/data/node7-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt 2>&1;
            echo "============================="
            
            # Run kill_server.sh script on memory nodes
            echo "Kill server process on memory nodes"
            ssh skv-node3 "/bin/bash -c 'cd $dm_tree_dir/script && bash kill_server.sh'; exit;"
            echo "============================="

            # After completion, copy data from compute nodes to node7
            echo "Copying data from compute nodes to node7"
            for n in $node; do
                echo "Copying data from node$n to node7's AE/Data directory"
                # Use scp to copy data files from each compute node to node7's AE/Data directory
                scp "skv-node$n:$dm_tree_dir/data/node$n-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt" "$ae_data_dir/"
            done
            echo "Copying local data from node7 to node7's AE/Data directory"
            cp "$dm_tree_dir/data/node7-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt" "$ae_data_dir/"
            echo "============================="

            echo "Finished $dis-$file_name with thread $thread"
            wait
            echo "============================="
        done
    done
done

echo "============================="
echo "All dLSM tasks are finished"
echo "All experiment results are saved in: $dm_tree_dir/data"
echo "============================="
