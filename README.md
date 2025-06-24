# DMTree

## Introduction

This is the implementation repository of our *FAST'26* paper: **DMTree: Towards Efficient Tree Indexing on Disaggregated Memory via Compute-side Collaborative Design**.

* `DMTree/`: includes the implementation of the DMTree prototype.

## Artifact Instructions

Please refer to the [AE_INSTRUCTION.md](AE_INSTRUCTION.md) for details.

**Note:** The content below can be safely ignored for Artifact Evaluation, as all relevant AE information is documented separately in the provided AE instruction.

## Prerequisites

### Dependencies

* The toolkit is running under Linux (e.g., Ubuntu 20.04) with a C++ compiler (e.g., g++). 
* Install Mellanox OFED-5.X.

The packages can be directly installed via `apt-get` and `pip` package managers:

```shell 
`g++`，`cmake`，`libssl`，`snappy`，`memcached`，`boost`，`city-hash`
```

## Environment setup

To build the ELECT prototype and YCSB benchmark, simply run the following commands:

```shell
cd DMTree
mkdir build && cd build && cmake .. && make -j
```

## Running 

To test the DMTree prototype, we need to run the following steps:

1. Run the DMTree server on memory nodes.
2. Run the Micro/YCSB benchmark on compute nodes.

We describe the detailed steps below.

### Run the DMTree server on memory nodes.

On each memory node, launch the DMTree server with the following commands:

```shell
cd DMTree/build
./server
```

### Run the Micro/YCSB benchmark on compute nodes

After the server has been started on the memory nodes, execute the benchmark on each compute node using:

```shell
cd DMTree/build
./ycsbc 72 4 ycsb-c zipfian
```

- `72`: number of client threads
- `4`: number of coroutines per thread
- `ycsb-c`: workload type
- `zipfian`: workload distribution (can also use `uniform`)
