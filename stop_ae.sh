

#/bin/bash

trap 'kill $(jobs -p)' SIGINT

node="6 5 4 2 1"
for n in $node; do
    echo $n
    ssh skv-node$n "/bin/bash -c 'cd wgl/Revision-CX5; sudo sh ./kill_ycsb.sh'; exit;"
done
sudo sh ./kill_ycsb.sh;
ssh skv-node3 "/bin/bash -c 'cd wgl/Revision-CX5/SMART/build; sudo sh ./kill_client.sh'; exit;"

echo "Finished"