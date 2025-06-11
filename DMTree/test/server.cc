#include "DMTree.h"
#include <fstream>

int kCoroCnt = 4;
int kNodeCount = 7;
int kComputeNodeCount = 6;
int kMemoryNodeCount = 1;

DMVerbs* dmv;
DMTree* client;

int main(int argc, char* argv[]) {
	std::cout << "Running" << std::endl;

    DMConfig config;
    config.dsmSize = 80;
    config.machineNR = kNodeCount;
	config.ComputeNumber = kComputeNodeCount;
	config.MemoryNumber = kMemoryNodeCount;

    dmv = DMVerbs::getInstance(config);

    dmv->registerThread();
    dmv->set_barrier("init");
    dmv->set_barrier("loading");
    dmv->set_barrier("running");
    dmv->set_barrier("finish");

    int a = 0;
    while(a == 0) {
        std::cin>>a;
    }
	return 0;
}