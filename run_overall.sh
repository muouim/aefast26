

#/bin/bash

trap 'kill $(jobs -p)' SIGINT

index="DMTree FPTree Sherman SMART ROLEX dLSM CHIME"
node="6 5 4 2 1"

for i in $index; do
    for n in $node; do
        echo $n
        scp -r skv-node$n:~/wgl/Revision-CX5/$i/data ~/wgl/Data/node$n/$i;
    done
done

for i in $index; do
    cd Revision-CX5
    cp -r ~/wgl/Revision-CX5/$i/data ~/wgl/Data/node1/$i
    cd ..
done

echo "Finished"