#!/bin/bash

trap 'kill $(jobs -p)' SIGINT

# Define the directory as a variable
dm_tree_dir="~/aefast26/DMTree"

workloads="ycsb-c insert-only update-only scan-only"
threads="72"
distribution="zipfian"
node="6 5 4 2 1"

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
                ssh skv-node$n "cd $dm_tree_dir/build; nohup ./ycsbc $thread 4 $file_name $dis > $dm_tree_dir/data/node$n-exp0_dmtree_$file_name-$dis-thread$thread-coro4.txt 2>&1 &; exit;"
            done
            
            sleep 2
            echo "Running ycsbc on compute node 7"
            cd $dm_tree_dir/build && ./ycsbc $thread 4 $file_name $dis > $dm_tree_dir/data/node7-exp0_dmtree_$file_name-$dis-thread$thread-coro4.txt 2>&1;
            
            # Run kill_server.sh script on on memory nodes
            echo "Kill server process on memory nodes"
            ssh skv-node3 "/bin/bash -c 'cd $dm_tree_dir/script && bash kill_server.sh'; exit;"
            
            echo "Finished $dis-$file_name with thread $thread"
            wait
            echo "============================="
        done
    done
done

echo "============================="
echo "All DMTree tasks are finished"
echo "All experiment results are saved in: $dm_tree_dir/data"
echo "============================="
