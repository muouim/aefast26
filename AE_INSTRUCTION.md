# DMTree Artifact

This repository contains the artifact for the paper "**DMTree: Towards Efficient Tree Indexing on Disaggregated Memory via Compute-side Collaborative Design**", accepted at USENIX FAST'26.

## Artifact Overview

This artifact allows reviewers to reproduce the main experimental results of the paper, including:

-  DMTree outperforms existing state-of-the-art range indexes on DM for both point operations (i.e., searches, inserts, and updates) and range operations (i.e., scans) under various workloads (e.g., Micro-benchmarks and YCSB workloads).

We evaluate and compare **DMTree** with five state-of-the-art DM-optimized range indexes: **Sherman, ROLEX, SMART, dLSM, and CHIME**, all evaluated based on their open-source implementations.

- [Sherman *(SIGMOD'22)*](https://github.com/thustorage/Sherman) is a DM-optimized B+-tree.
- [ROLEX *(FAST'23)*](https://github.com/River861/ROLEX) is the latest learned index on DM.
- [SMART *(OSDI'23)*](https://github.com/dmemsys/SMART) is the latest radix tree on DM.
- [dLSM *(ICDE'23)*](https://github.com/ruihong123/dLSM) is the state-of-the-art DM-optimized LSM-tree.
- [CHIME *(SOSP'24)*](https://github.com/dmemsys/CHIME) is a DM-optimized hybrid index that combines the B+ tree with hopscotch hashing.

## Cluster setup

* We provide a server cluster consisting of 7 nodes (skv-node1 to skv-node7) for reproducing the experiments in our paper. One node, `skv-node7`, will be made available as the jump host for accessing the cluster and as the main node for running the reproduction scripts. 

* **The AEC may submit their SSH public key through a HotCRP comment.** After we have configured the key on our server, node7 will be accessible via the following command:

  ```shell
  ssh -P 6672 aefast26@222.195.68.87
  ```

* We sincerely apologize that, due to limited cluster resources, we are unable to provide each AEC with an independent execution environment. To prevent potential conflicts caused by concurrent usage, we have adopted an exclusive access notification mechanism in the following steps, ensuring that only one AEC can run experiments at any given time. 

## Environment setup (~15 minutes)

We provide scripts to set up the environment for the evaluation, including cloning the code repository and copying and compiling it across multiple cluster nodes. The scripts are tested on Ubuntu 20.04 LTS. 

**Step 1:** Clone the code repository `aefast26`.

- Due to potential network fluctuations, the clone operation might require a few retries to complete successfully.

```shell
git clone https://github.com/muouim/aefast26.git
```

**Step 2:** Ensure exclusive access to the cluster by running the locking script`lock_cluster.sh + reviewer ID `. This step prevents concurrent executions by other AECs and ensures consistent resource usage.

```shell
cd aefast26
bash lock_cluster.sh "reviewer A"
```

**Step 3:** Run the following script on `skv-node7`, including copying and compiling the code across multiple cluster nodes.

```shell
bash build_ae.sh
```

In the `build_ae.sh` script, we copy the code to all nodes and perform node-specific configuration and compilation. This includes modifying memory-related parameters for some baselines on memory nodes (which require larger memory sizes) and adjusting compute node-specific settings for certain baselines (e.g., specifying the compute node ID in the test code for dLSM).

To prevent repeated compilation and concurrent execution from disrupting the established environment, we have implemented a tracking and checking mechanism for the environment setup status when running the `build_ae.sh` script:

- If the script has not been executed before and the code has not been copied or compiled, the script will be executed.

- If the script is currently being executed, a message will prompt: ”`The script is already running, please wait.`“

- If the script has already been executed, a message will prompt: "`Environment setup is complete, no need to run the script again.`"

- If the AEC wishes to re-run the setup due to unexpected issues or configuration changes, the status can be reset by removing the flag file via:

  ```shell
  rm /tmp/build_ae.flag
  ```

## Evaluations

This section describes how to reproduce the major experiments in our paper. **We suggest running the scripts of 'Micro experiment' first, which can reproduce the main results of our paper while including most of the functionality verification** (i.e., DMTree outperforms existing state-of-the-art range indexes on DM for both point operations (i.e., searches, inserts, and updates) and range operations (i.e., scans)).

### Micro experiment

#### Clean up memory

The `reset_memory.sh` script is used to clear the Linux page cache, ensuring sufficient available memory before running performance experiments. This helps eliminate memory pressure caused by residual cached data from previous runs.

```shell
bash reset_memory.sh
```

#### Exp#11-12: Performance with Micro-benchmarks (~11 hours)

We provide this simple experiment to verify our main experimental results quickly: **DMTree outperforms existing state-of-the-art range indexes on DM for both point operations (i.e., searches, inserts, and updates) and range operations (i.e., scans).** Specifically, we preload 1 billion KV pairs and perform 100 million KV operations (including search/insert/update/scan).

You can run this simple experiment via the following command:

```shell
nohup bash run_simple.sh >run_simple.output 2>&1 &
```

This launches the experiment in the background and redirects the output to a log file, allowing you to safely close the terminal without interrupting the execution. 

- You can monitor the execution progress in the `run_simple.output` file, which indicates the current baseline being tested. 
- **Once the script has completed all experiments, you will see the message**`---------- All phases completed. ----------` **at the end of the output file**.

The `run_simple.sh` script executes micro-benchmark experiments across all baseline systems and leverages Python scripts to structure and visualize the results for comparative analysis.

To prevent repeated experiments and concurrent execution from disrupting the running experiments, we have implemented a tracking and checking mechanism for the simple experiment status when running the `run_simple.sh` script:

- If the script has not been executed before and the code has not been copied or compiled, the script will be executed.

- If the script is currently being executed, a message will prompt: ”`The script is already running, please wait.`“

- If the script has already been executed, a message will prompt: "`Simple experiment is complete, no need to run the script again.`".

- If the AEC wishes to re-run the simple experiment due to unexpected results or issues, the status can be reset by removing the flag file with:

  ```shell
  rm /tmp/simple_exp.flag
  ```

**Note:** If you wish to release the cluster for use by others after completing the current experiment, **please run the command below to update the cluster's release status.**

```shell
bash unlock_cluster.sh
```

#### Exp#11-12: Result analysis

The raw experimental results are stored in the `AE/Data` directory. The processed results are organized and written to the files `simple_results_uniform.csv` and `simple_results_zipfian.csv`. For visual comparison, we also generate bar charts saved as `simple_uniform.pdf` and `simple_zipfian.pdf`.

As shown below, we present the processed results and corresponding plots generated by the test script.

**simple_results_uniform.csv**

```
Index,Workload,Total,Node1,Node2,Node4,Node5,Node6,Node7
dmtree,ycsb-c,49.69586,8.3214,8.4839,8.39724,8.07157,8.22343,8.19832
dmtree,insert-only,26.372739999999997,4.4403,4.54854,4.17407,4.23682,4.60402,4.36899
dmtree,update-only,31.795920000000002,5.27421,5.29733,5.28315,5.34774,5.36419,5.2293
dmtree,scan-only,3.446158,0.572914,0.575549,0.572203,0.575403,0.57504,0.575049
fptree,ycsb-c,23.714789999999997,3.96312,3.97181,3.9583,3.96514,3.95907,3.89735
fptree,insert-only,13.52334,2.22194,2.25822,2.24564,2.27178,2.27274,2.25302
fptree,update-only,13.69716,2.28294,2.28727,2.27213,2.285,2.28717,2.28265
fptree,scan-only,2.654987,0.442648,0.442568,0.442031,0.442587,0.442552,0.442601
sherman,ycsb-c,9.81188,1.63574,1.63579,1.6328,1.63613,1.63577,1.63565
sherman,insert-only,5.981216,0.998046,0.998478,0.995345,0.999298,0.994832,0.995217
sherman,update-only,8.77102,1.46285,1.46291,1.45914,1.46308,1.46301,1.46003
sherman,scan-only,2.990589,0.498535,0.498477,0.497884,0.498578,0.49856,0.498555
smart,ycsb-c,48.28557,6.58257,9.18799,6.40419,8.93014,8.11444,9.06624
smart,insert-only,11.53051,1.71435,1.97921,1.96133,1.91434,2.05267,1.90861
smart,update-only,21.919349999999998,3.40476,3.71655,3.68659,3.6647,3.71745,3.7293
smart,scan-only,1.041088,0.173086,0.174587,0.172613,0.173529,0.174248,0.173025
rolex,ycsb-c,10.17066,1.69627,1.69603,1.68939,1.69627,1.69641,1.69629
rolex,insert-only,7.06437,1.17714,1.1776,1.177,1.17762,1.1775,1.17751
rolex,update-only,7.07585,1.17991,1.17994,1.17607,1.17998,1.18003,1.17992
rolex,scan-only,3.391118,0.565502,0.565522,0.563582,0.565517,0.565492,0.565503
dlsm,ycsb-c,18.36991,3.21127,2.92119,3.12477,3.025,3.02454,3.06314
dlsm,insert-only,4.715047,0.696512,0.883874,0.771909,0.779352,0.754986,0.828414
dlsm,update-only,23.23445,2.02157,2.0664,10.2327,2.1009,1.9844,4.82848
dlsm,scan-only,0.7661690000000001,0.132276,0.131247,0.118661,0.136749,0.130517,0.116719
chime,ycsb-c,31.38408,5.49949,3.81008,5.37363,5.50274,5.70717,5.49097
chime,insert-only,7.435649999999999,1.23096,1.24667,1.23782,1.24351,1.23578,1.24091
chime,update-only,18.45321,3.08806,2.99077,3.08156,3.09312,3.10516,3.09454
chime,scan-only,2.538482,0.422086,0.405211,0.419598,0.428614,0.43807,0.424903
```

**simple_results_zipfian.csv**

```shell
Index,Workload,Total,Node1,Node2,Node4,Node5,Node6,Node7
dmtree,ycsb-c,53.14693,9.03308,9.02831,8.79015,8.48879,8.91789,8.88871
dmtree,insert-only,26.38105,4.39275,4.49302,4.34678,4.31152,4.58725,4.24973
dmtree,update-only,29.02227,4.05761,4.94014,4.67556,4.98945,5.11801,5.2415
dmtree,scan-only,3.4390020000000003,0.572354,0.57411,0.570717,0.574015,0.57396,0.573846
fptree,ycsb-c,26.38646,4.38804,4.40993,4.39224,4.39771,4.40559,4.39295
fptree,insert-only,13.49708,2.24084,2.25663,2.24714,2.24957,2.25919,2.24371
fptree,update-only,14.77921,2.4547,2.46837,2.43277,2.46653,2.48649,2.47035
fptree,scan-only,2.6599909999999998,0.443154,0.443439,0.443034,0.443496,0.443498,0.44337
sherman,ycsb-c,9.80682,1.63447,1.63479,1.63307,1.63492,1.63469,1.63488
sherman,insert-only,5.982340999999999,0.998655,0.997865,0.995248,1.00002,0.995225,0.995328
sherman,update-only,1.897112,0.316186,0.315484,0.314938,0.315559,0.317741,0.317204
sherman,scan-only,3.014283,0.502456,0.50244,0.502058,0.502442,0.502452,0.502435
smart,ycsb-c,51.152860000000004,6.86627,9.21044,7.62574,8.77758,9.3365,9.33633
smart,insert-only,11.441650000000001,1.72969,1.95853,1.82413,2.00204,2.00797,1.91929
smart,update-only,24.898239999999998,3.67283,4.37875,3.72425,4.30601,4.41896,4.39744
smart,scan-only,1.049804,0.176254,0.175273,0.173624,0.174355,0.175623,0.174675
rolex,ycsb-c,11.33306,1.88964,1.88962,1.8852,1.8896,1.88975,1.88925
rolex,insert-only,7.06801,1.17844,1.17837,1.17615,1.1784,1.17833,1.17832
rolex,update-only,7.893560000000001,1.31615,1.31663,1.31142,1.31661,1.31707,1.31568
rolex,scan-only,3.3781749999999997,0.563232,0.563207,0.562062,0.563236,0.563217,0.563221
dlsm,ycsb-c,18.03255,3.36123,2.97891,2.93221,2.90981,2.89991,2.95048
dlsm,insert-only,5.184105,0.732869,1.00827,0.875831,0.789125,0.943514,0.834496
dlsm,update-only,40.14341,5.20028,5.67613,6.23658,7.04943,7.73673,8.24426
dlsm,scan-only,0.77451,0.134825,0.125945,0.124501,0.135181,0.12716,0.126898
chime,ycsb-c,40.37225,7.14846,5.33814,7.02093,6.86335,7.13929,6.86208
chime,insert-only,7.4409,1.23996,1.24562,1.22614,1.2461,1.23996,1.24312
chime,update-only,18.59494,3.1023,3.0827,3.09694,3.09088,3.1103,3.11182
chime,scan-only,2.718992,0.447759,0.455589,0.45241,0.453392,0.452061,0.457781
```

The experimental results in `simple_results_uniform.csv` are visualized as bar charts, as shown in the figure `simple_uniform.pdf`.

<img src=".\AE_INSTRUCTION.assets\image-20250624203054068.png" alt="image-20250624203054068" style="zoom:33%;" />

The experimental results in `simple_results_zipfian.csv` are visualized as bar charts, as shown in the figure `simple_zipfian.pdf`.

<img src=".\AE_INSTRUCTION.assets\image-20250624203359124.png" alt="image-20250624203359124" style="zoom:33%;" />

The output experimental results correspond to those presented in **Figures 11 and 12** of the original paper. To enable quick experimental verification, we only provide the bottleneck performance—**i.e., the performance of each baseline under each workload at the maximum thread count**.

As shown in the original overall experiment figures, the red boxes highlight the data produced by the Micro experiment, representing the performance of each baseline under each workload at the maximum thread count.

<img src=".\AE_INSTRUCTION.assets\image-20250624205627370.png" alt="image-20250624205627370" style="zoom: 67%;" />

### YCSB experiment

We provide this YCSB experiment to verify our overall results: **DMTree outperforms existing state-of-the-art range indexes on DM for both point operations (i.e., searches, inserts, and updates) and range operations (i.e., scans) under various workloads.** Specifically, we preload 1 billion KV pairs and perform 100 million KV operations (including YCSB A/B/C/D/E/F).

#### Clean up memory

The `reset_memory.sh` script is used to clear the Linux page cache, ensuring sufficient available memory before running performance experiments. This helps eliminate memory pressure caused by residual cached data from previous runs.

```shell
bash reset_memory.sh
```

#### Exp#14: Performance with YCSB core workloads (-15 hours)

***Running:***

You can run this ycsb experiment via the following command:

```shell
nohup bash run_ycsb.sh >run_ycsb.output 2>&1 &
```

This launches the experiment in the background and redirects the output to a log file, allowing you to safely close the terminal without interrupting the execution. 

- You can monitor the execution progress in the `run_ycsb.output` file, which indicates the current baseline being tested. 
- Once the script has completed all experiments, you will see the message`---------- All phases completed. ----------` at the end of the output file.

To prevent repeated experiments and concurrent execution from disrupting the running experiments, we have implemented a tracking and checking mechanism for the simple experiment status when running the `run_ycsb.sh` script:

- If the script has not been executed before and the code has not been copied or compiled, the script will be executed.

- If the script is currently being executed, a message will prompt: ”`The script is already running, please wait.`“

- If the script has already been executed, a message will prompt: "`YCSB experiment is complete, no need to run the script again.`".

- If the AEC wishes to re-run the YCSB experiment due to unexpected results or other issues, the status can be reset by removing the flag file using:

  ```shell
  rm /tmp/ycsb_exp.flag
  ```

***Results:*** 

The raw experimental results are stored in the `AE/Data` directory. The processed results are organized and written to the files `ycsb_results_uniform.csv` and `ycsb_results_zipfian.csv`. For visual comparison, we also generate bar charts saved as `ycsb_uniform.pdf` and `ycsb_zipfian.pdf`.

**Note:** If you wish to release the cluster for use by others after completing the current experiment, **please run the command below to update the cluster's release status.**

```shell
bash unlock_cluster.sh
```
