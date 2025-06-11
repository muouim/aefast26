# DMTree

This is the implementation repository of our “Resolving the Performance Tradeoffs of Tree Indexing on Disaggregated Memory”

## Introduction

Encrypted.

We study how .

- In the.

The toolkit 

## Publication

- 

## Preparation

The toolkit is running under Linux (e.g., Ubuntu 20.04) with a C++ compiler (e.g., g++). 

Install Mellanox OFED.

```shell
cd DMTree
chmod u+x ./script/*.sh
sh ./script/installMLNX.sh
```

To run the programs, you need to install/compile the following dependencies.

- `g++`，`cmake`，`libssl`，`snappy`，`memcached`，`boost`，`city-hash`

The commands for running the script to install libraries and tools are as follows:

```shell
sh ./script/installLibs.sh
```

## Configuration

The DMTree , in order to .

**1. Configure cluster servers:** The ips of servers can be specified in `./config.json`:

```json
"GlobalConfig": {
    "_indexMark": 2
},
"Client": {
    "_ClientIPList": {
        "10.0.0.66": 1,
			......
    }
},
"Server": {
    "_ServerIPList": {
        "10.0.0.62": 1,
			......
    }
}
```

- `_indexMark` specifies the type of the index: `2` for DMTree, `1` for Cuckoo.
- `_ClientIPList`.
- `_ServerIPList`.

some parameters need to be specified in `./include/structure.h`

```c++
#define CS_NUM 1 // number of the client servers
#define MS_NUM 1 // number of the memory servers
```

**2. Configure memcached server:** The memcached server can be specified by two arrays in `memcached.conf`:

```json
10.0.0.66
88888
```

- The first line specifies the ip of the memcached server.
- The second line specifies the open port of the memcached server.

## Prototype

**1. Client:**  Execute the following command in **client servers** to launch a client:

```shell
./ycsbc -db <db_name> -threads <thread_num> -P <workload_num>
```

- `-db <db_name>` specifies the name of the running db.
- `-thread <thread_num>` specifies the number of the client threads launched in each server.
- `-P <workload_num>` specifies the running workloads.

**2. Server:**  Execute the following command in **memory servers** to launch a server:

```shell
./server
```

- run `./script/hugepage.sh` to request huge pages before launching a server.

**3. Run sample:** The sample script `script/run_sample.sh` of DMTree.

```shell
workloads="../workloads_zip/workloadc.spec ..."
threads="36 ..."
client1="localhost"
server1="kvgroup@skv-node5"
```

- `workloads` specifies the workloads to be evaluated.
- `threads` specifies the number of the client threads launched in each server.

- `client1` specifies ，`server1` specifies .

Type the following commands to compile and run the sample script of DMTree.

```shell
cd DMTree/
mkdir build
cd build/ && cmake .. && make
cp ../script/*.sh ./
chmod u+x ./*.sh
sh run_sample.sh
```


## Outputs

The output format of distribution-based attack is shown as follows. Since the information leakage does not help improve the precision of the inference attack, we always fix the leakage rate at zero (i.e., ciphertext-only attack).

```c
=========Distribution-based Attack=========
Parameters: u: , r: , t:, leakage_rate: 0
Source backup: YY-MM
Target backup: YY-MM
Total number of unique ciphertext chunks: X
[Zero Leakage rate: ...]
Successfully inferred following chunks:
......

Involve:Y
Correct:Z
Inferred rate: ......
Precision: ......
```

- `X` is the number of unique ciphertext chunks in the encryption of the target backup.
- `Y` is the number of (unique) chunks that are inferred by the attacks.
- `Z` is the number of (unique) chunks that can be successfully inferred by the attacks.

The inference rate is computed by `Z/X` and the precision is computed by `Z/Y`. Note that the inference results are slightly affected by the sorting algorithm in frequency analysis. The reason is that different sorting algorithms may break tied chunks (that have the same frequency counts) in different ways and lead to (slightly) different results. Also, different g++ versions may lead to slightly different results.
