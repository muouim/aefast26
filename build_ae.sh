

#/bin/bash

trap 'kill $(jobs -p)' SIGINT

node="6 5 4 2 1"
current_dir="$PWD"
folder_name=$(basename "$PWD")

# build code for compute nodes
for n in $node; do
    ssh skv-node$n "/bin/bash -c 'sudo sysctl -w vm.nr_hugepages=12768'; exit"
done
sudo sysctl -w vm.nr_hugepages=12768;

id=0
for n in $node; do
    scp -r "$current_dir" skv-node$n:~/
    ssh skv-node$n "/bin/bash -c 'cd $folder_name && python3 ./AE/default.py --node_id $id && sh ./compile.sh'; exit;"
    ((id++))
done
python3 ./AE/default.py --node_id 0 && sh ./compile.sh

# build code for the memory node
ssh skv-node3 "/bin/bash -c 'sudo sysctl -w vm.nr_hugepages=82768'; exit"  
scp -r "$current_dir" skv-node3:~/
ssh skv-node3 "/bin/bash -c 'cd $folder_name && python3 ./AE/default.py -1 && sh ./compile.sh'; exit;"

echo "Finished"