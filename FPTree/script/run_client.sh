#/bin/bash

sleep 10;
`nohup ./server >> log.out 2>&1 & echo $! > dmtree_log.out`; echo "server"; 
sleep 100;