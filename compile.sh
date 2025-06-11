

#/bin/bash

trap 'kill $(jobs -p)' SIGINT

index="DMTree FPTree Sherman SMART ROLEX dLSM CHIME"

for i in $index; do
    cd $i
    mkdir build && cd build && cmake .. && make -j
    cd ..
    cd ..
done

echo "Finished"