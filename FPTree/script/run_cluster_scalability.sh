

#/bin/bash

trap 'kill $(jobs -p)' SIGINT

workloads="ycsb-c insert-only update-only scan-only"
threads="72 1 6 12 24 36 48 60"
distribution="zipfian uniform"
node="6 5 4 2 1"

for dis in $distribution; do
    for thread in $threads; do
        for file_name in $workloads; do
            echo "Running Redis with for $dis-$file_name thread $thread"
            ssh skv-node3 "/bin/bash -c 'cd ./wgl/Revision-CX5/DMTree/build && bash restart.sh'; exit;"
            ssh skv-node3 "/bin/bash -c 'cd ./wgl/Revision-CX5/DMTree/build && sh ./run_client.sh'; exit;"
            echo "run node"
            for n in $node; do
                sleep 2
                ssh skv-node$n "cd ./wgl/Revision-CX5/DMTree/build; nohup ./ycsbc $thread 4 $file_name $dis>../data/exp0_dmtree_$file_name-$dis-thread$thread-coro4.txt 2>&1 &; exit;"
                echo "node $n"
            done
            sleep 2
            ./ycsbc $thread 4 $file_name $dis>../data/exp0_dmtree_$file_name-$dis-thread$thread-coro4.txt 2>&1;
            echo "node 7"
            ssh skv-node3 "/bin/bash -c 'cd ./wgl/Revision-CX5/DMTree/build && sh ./kill_client.sh'; exit;"
            echo "Finish $file_name"
            wait
        done
    done
done

echo "Finished"