#/bin/bash

sleep 30;
`nohup ./benchmark 7 100 1 >> log.out 2>&1 & echo $! > sherman_log.out`; echo "server"; 
sleep 100;