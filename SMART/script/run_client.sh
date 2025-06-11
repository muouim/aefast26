#/bin/bash

sleep 10;
`nohup ./ycsb_test 7 72 8 randint a >> log.out 2>&1 & echo $! > smart_log.out`; echo "server"; 
sleep 50;