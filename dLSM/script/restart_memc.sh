#!/bin/bash

# kill old me
cat /tmp/memcached.pid | xargs kill
sleep 2

# launch memcached
memcached -u root -l 10.0.0.63 -p  18888 -c 10000 -d -P /tmp/memcached.pid
sleep 2
# init 
echo -e "set serverNum 0 0 1\r\n0\r\nquit\r" | nc 10.0.0.63 18888
sleep 2

echo -e "set clientNum 0 0 1\r\n0\r\nquit\r" | nc 10.0.0.63 18888
sleep 2
