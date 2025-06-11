#!/bin/bash  

# ycsbc id
pids=$(ps -ef | grep ycsbc | grep -v grep | awk '{print $2}')

# kill ycsb process
if [ -n "$pids" ]; then  
    echo "kill process: $pids"
    kill $pids  
else  
    echo "no ycsb process"
fi  