#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <future>
#include <unistd.h>
#include "Timer.h"
#include "Tree.h"
#include "util/ycsb.h"
#include "util/timer.h"

#include <city.h>
#include <stdlib.h>
#include <thread>
#include <time.h>

#define USE_CORO
#define SHERMAN_LATENCY
#define SHERMAN_MAX_LATENCY_SIZE 100000

std::atomic<bool> need_stop(false);

uint64_t num_threads = 0;
int kCoroCnt = 4;
int kNodeCount = 7;
int kComputeNodeCount = 6;
int kMemoryNodeCount = 1;

using namespace std;
using namespace sds;
using OpRecord = util::OpRecord;

Tree *tree;
DSM *dsm;

bool rehash_key_ = true;
util::WorkloadBuilder* builder_[MAX_APP_THREAD];

extern uint64_t rdma_cnt[MAX_APP_THREAD][MAX_MACHINE];
extern uint64_t rdma_bw[MAX_APP_THREAD][MAX_MACHINE];

#ifdef SHERMAN_LATENCY
uint64_t exeTime[MAX_APP_THREAD][define::kMaxCoro];
struct timespec time_start[MAX_APP_THREAD][define::kMaxCoro];
struct timespec time_end[MAX_APP_THREAD][define::kMaxCoro];

double request_latency[MAX_APP_THREAD][define::kMaxCoro];
uint64_t dis_latency[MAX_APP_THREAD][define::kMaxCoro][SHERMAN_MAX_LATENCY_SIZE];
#endif

thread_local CoroCall worker[define::kMaxCoro];
thread_local CoroCall master;
thread_local CoroQueue busy_wait_queue;

thread_local uint64_t thread_finish_ops;
thread_local uint64_t thread_finish_coros;
atomic<uint64_t> finish_thread_count;

atomic<uint64_t> load_count;

void reset() {
    cout << "A part finished." << endl;
    for(int i = 0; i < MAX_APP_THREAD; i++) {
        for(int j = 0; j < MAX_MACHINE; j++){
            rdma_cnt[i][j] = 0;
            rdma_bw[i][j] = 0;
        }
    }
#ifdef SHERMAN_LATENCY
    for(int i = 0; i < MAX_APP_THREAD; i++) {
	    for(int j = 0; j < define::kMaxCoro; j++) {
            request_latency[i][j] = 0;
            memset(dis_latency[i][j], 0, sizeof(dis_latency[i][j]));
        }
	}
#endif
}

Key str2key(string &key) {
    Key t_key;
    memcpy(t_key.key, key.c_str(), sizeof(t_key));
    return t_key;
}

std::string build_key_str(uint64_t key) {
    if(rehash_key_) {
        key = util::FNVHash64(key);
    }
    auto str = std::to_string(key);
    if(str.size() < KEY_SIZE) {
        return std::string(KEY_SIZE - str.size(), '0') + str;
    } else {
        return str.substr(0, KEY_SIZE);
    }
}

void do_transaction() {

    int th_id = dsm->getMyThreadID();
    auto& builder = builder_[th_id];

#ifdef SHERMAN_LATENCY
	clock_gettime(CLOCK_REALTIME, &time_start[th_id][0]);
#endif

    OpRecord record;
    builder->fill_record(record);

    auto key = build_key_str(record.key);
    int rc;
    if(record.type == util::INSERT) {
        Value value = 12;
        tree->insert(str2key(key), value);
    }
    if(record.type == util::READ) {
        Value value;
        if(!tree->search(str2key(key), value)) {
            std::cout<<"No Find Key: "<<key<<std::endl;
        }
    }
    if(record.type == util::UPDATE) {
        Value value = 12;
        tree->insert(str2key(key), value);
    }
    if(record.type == util::SCAN) {
        std::vector<std::string> vec;
        Value values[1000];
        // std::cout<<"-----------------------------------------------"<<std::endl;
        // std::cout<<"scan form: "<<record.key <<", to "<< record.key + record.scan_len<<std::endl;
        tree->range_query(str2key(key), kKeyMax, record.scan_len, values);
        // std::cout<<"-----------------------------------------------"<<std::endl;
    }
    if(record.type == util::READMODIFYWRITE) {
        Value value;
        value = 12;
        tree->search(str2key(key), value);
        tree->insert(str2key(key), value);
    }

#ifdef SHERMAN_LATENCY
	clock_gettime(CLOCK_REALTIME, &time_end[th_id][0]);
	exeTime[th_id][0] =
	    (time_end[th_id][0].tv_sec - time_start[th_id][0].tv_sec) * 1000000000 +
	    (time_end[th_id][0].tv_nsec - time_start[th_id][0].tv_nsec);
	request_latency[th_id][0] += exeTime[th_id][0];
	if(exeTime[th_id][0] / 100 >= SHERMAN_MAX_LATENCY_SIZE) {
		cout << "long time " << exeTime[th_id][0] << " ns" << endl;
	} else {
		dis_latency[th_id][0][exeTime[th_id][0] / 100]++;
	}
#endif
}

void coro_worker(CoroYield& yield, uint64_t operation_count, uint64_t thread_count,
                 int coro_id) {
    CoroContext ctx;
    ctx.coro_id = coro_id;
    ctx.master = &master;
    ctx.yield = &yield;
    ctx.hot_wait_queue = &busy_wait_queue;

    int th_id = dsm->getMyThreadID();
    auto& builder = builder_[th_id];

    while(thread_finish_ops < operation_count) {
#ifdef SHERMAN_LATENCY
		clock_gettime(CLOCK_REALTIME, &time_start[th_id][coro_id]);
#endif
        OpRecord record;
        builder->fill_record(record);

        auto key = build_key_str(record.key);
        int rc;
        if(record.type == util::INSERT) {
            Value value = 12;
            tree->insert(str2key(key), value, &ctx, coro_id);
        }
        if(record.type == util::READ) {
            Value value;
            if(!tree->search(str2key(key), value, &ctx, coro_id)) {
                std::cout<<"No Find Key: "<<key<<std::endl;
            }
        }
        if(record.type == util::UPDATE) {
            Value value = 12;
            tree->insert(str2key(key), value, &ctx, coro_id);
        }
        if(record.type == util::SCAN) {
            std::vector<std::string> vec;
            Value values[1000];
            tree->range_query(str2key(key), kKeyMax, record.scan_len, values, &ctx, coro_id);
        }
        if(record.type == util::READMODIFYWRITE) {
            Value value;
            value = 12;
            tree->search(str2key(key), value, &ctx, coro_id);
            tree->insert(str2key(key), value, &ctx, coro_id);
        }
#ifdef SHERMAN_LATENCY
		clock_gettime(CLOCK_REALTIME, &time_end[th_id][coro_id]);
		exeTime[th_id][coro_id] = (time_end[th_id][coro_id].tv_sec -
		                           time_start[th_id][coro_id].tv_sec) *
		                              1000000000 +
		                          (time_end[th_id][coro_id].tv_nsec -
		                           time_start[th_id][coro_id].tv_nsec);
		request_latency[th_id][coro_id] += exeTime[th_id][coro_id];
		if(exeTime[th_id][coro_id] / 100 >= SHERMAN_MAX_LATENCY_SIZE) {
			cout << "long time " << exeTime[th_id][coro_id] << " ns" << endl;
		} else {
			dis_latency[th_id][coro_id][exeTime[th_id][coro_id] / 100]++;
		}
#endif
        thread_finish_ops++;
    }
    thread_finish_coros++;
    if(thread_finish_coros == kCoroCnt) {
        finish_thread_count++;
    std::cout << "thread finish " << dsm->getMyThreadID() <<" "<<thread_finish_coros<< ", num "
              << finish_thread_count.load() << std::endl;
    }

    std::cout << "coro finish " << dsm->getMyThreadID() <<" "<<thread_finish_coros<< ", num "
              << thread_finish_ops << std::endl;

    if(finish_thread_count.load() == thread_count) {
        need_stop.store(true, std::memory_order_release);
        std::cout<<"now need stop"<<std::endl;
	}
	while(!need_stop.load(std::memory_order_acquire)) {
		yield(master);
	}
}

void coro_master(CoroYield& yield, int coro_cnt) {

    for(int i = 0; i < coro_cnt; ++i) {
        yield(worker[i]);
    }

    while(!need_stop.load(std::memory_order_acquire)) {
        ibv_wc wc[16];
        int res = dsm->poll_rdma_cqs(wc);

        for(int i = 0; i < res; i++) {
            yield(worker[wc[i].wr_id]);
        }
        if(!busy_wait_queue.empty()) {
            auto next_coro = busy_wait_queue.front();
            busy_wait_queue.pop();
            yield(worker[next_coro]);
        }
	}

	for(int i = 0; i < coro_cnt; ++i) {
		yield(worker[i]);
	}
}

void run_coroutine(uint64_t operation_count, uint64_t thread_count, int coro_cnt) {

    thread_finish_ops = 0;
    thread_finish_coros = 0;
    std::cout << "thread start " << dsm->getMyThreadID() << ", num "
              << operation_count << std::endl;

    using namespace std::placeholders;
    assert(coro_cnt <= define::kMaxCoro);
    for(int i = 0; i < coro_cnt; ++i) {
        worker[i] = CoroCall(std::bind(&coro_worker, std::placeholders::_1,
                                       operation_count, thread_count, i));
    }
    master = CoroCall(std::bind(&coro_master, std::placeholders::_1, coro_cnt));

    master();

    std::cout << "thread finish " << dsm->getMyThreadID() << ", num "
              << thread_finish_ops << std::endl;
}

uint64_t thread_load(uint64_t start, uint64_t num_ops) {

    dsm->registerThread();
    uint64_t count = 0;
	cout << "load range: [" << start << "," << start + num_ops << "]"
	     << std::endl;

    for(uint64_t i = 0; i < num_ops; ++i) {
        if(load_count++ % 100000 == 0) {
            printf("load %d times\n", load_count.load());
        }
		auto key = build_key_str(start + i);
        Value value = 12;
        tree->insert(str2key(key), value);
        count++;
    }
    return count;
}

uint64_t thread_warm(uint64_t start, uint64_t num_ops) {

    dsm->registerThread();
    uint64_t count = 0;
	cout << "warm range: [" << start << "," << start + num_ops << "]"
	     << std::endl;

    for(uint64_t i = 0; i < num_ops; ++i) {
		auto key = build_key_str(start + i);
        Value value;
        if(!tree->search(str2key(key), value)){
            std::cout<<"No Find Key: "<<key<<std::endl;
        }
        count++;
    }
    return count;
}

uint64_t thread_run(const uint64_t num_ops, const uint64_t tran_ops) {

    dsm->registerThread();
    uint64_t count = 0;

#ifdef USE_CORO
    run_coroutine(num_ops, num_threads, kCoroCnt);
    count += num_ops;
#else
    for(uint64_t i = 0; i < num_ops; ++i) {
        do_transaction();
        count++;
    }
#endif
    assert(count == num_ops);
    return count;
}

extern bool enable_cache;

int main(const int argc, const char* argv[]) {
    
    load_count.store(0);
    finish_thread_count.store(0);

    DSMConfig config;
    config.machineNR = kNodeCount;
    dsm = DSM::getInstance(config);

    dsm->registerThread();
    tree = new Tree(dsm);

    cout << "creating benchmark" << endl;
	num_threads = stoi(argv[1]);
    kCoroCnt = stoi(argv[2]);
    string worloads = argv[3];
	string distribution = argv[4];

    // per-load key-value entries
    dsm->resetThread();
    vector<future<uint64_t>> actual_ops;
    uint64_t perload_ops = 1000000000;

	int tran_ops = 100000000;
    int sum = 0;

	for(int i = 0; i < num_threads; ++i) {
		uint64_t insert_start =
		    perload_ops +
		    (dsm->getMyNodeID() % kComputeNodeCount) *
		        (tran_ops / kComputeNodeCount);
		builder_[i] = util::WorkloadBuilder::Create(worloads.c_str(), distribution, perload_ops,
		                                            insert_start, 0.99);
		assert(builder_[i]);
	}
    cout << "creating benchmark finish" << endl;
    dsm->barrier("init", kComputeNodeCount);

    // per-load key-value entries
	num_threads = 72;
    uint64_t start = (dsm->getMyNodeID() % kComputeNodeCount) *
                        (perload_ops / kComputeNodeCount);
    uint64_t thread_op = perload_ops / kComputeNodeCount / num_threads;

    for(int i = 0; i < num_threads; ++i) {
        sleep(1);
        if(i == (num_threads - 1)) {
            thread_op +=
                (perload_ops % (kComputeNodeCount * num_threads));
            thread_op += kComputeNodeCount;
        }
        actual_ops.emplace_back(
            async(launch::async, thread_load, start, thread_op));
        start += thread_op;
    }
    assert((int)actual_ops.size() == num_threads);

    for(auto& n : actual_ops) {
        assert(n.valid());
        sum += n.get();
    }
    cerr << "# Loading records:\t" << sum << endl;

    dsm->barrier("loading", kComputeNodeCount);
    dsm->resetThread();

    // read key-value entries to fill up the cache
    actual_ops.clear();
	start = 0;
	thread_op = perload_ops / num_threads;

	for(int i = 0; i < num_threads; ++i) {
		if(i == (num_threads - 1)) {
			thread_op += (perload_ops % num_threads);
            thread_op += kComputeNodeCount;
		}
		actual_ops.emplace_back(
		    async(launch::async, thread_warm, start, thread_op));
        start += thread_op;
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for(auto& n : actual_ops) {
        assert(n.valid());
        sum += n.get();
    }
    cerr << "# Warm records:\t" << sum << endl;
    reset();

    dsm->barrier("running", kComputeNodeCount);
    dsm->resetThread();

    // perform transactions
	num_threads = stoi(argv[1]);
    actual_ops.clear();
    utils::Timer<double> timer;
    timer.Start();
    for(int i = 0; i < num_threads; ++i) {
        actual_ops.emplace_back(async(launch::async, thread_run,
                                      tran_ops / kComputeNodeCount / num_threads, tran_ops));
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for(auto& n : actual_ops) {
        assert(n.valid());
        sum += n.get();
    }
    double duration = timer.End();

	dsm->barrier("finish", kComputeNodeCount);

	cout << "----------------------------" << endl;
    cout << "Number of Thread: " << num_threads << endl;

#ifdef USE_CORO
    cout << "Number of OutStanding WR: " << kCoroCnt << endl;
#endif
    cout << "# Transaction throughput (MOPS): "
         << (tran_ops / kComputeNodeCount / num_threads * num_threads) / duration / 1000 / 1000 << endl;
    cout << "Total Time: " << duration << "s" << endl;
    cout << "Latency: " << duration / (tran_ops / kComputeNodeCount / num_threads * num_threads) * 1000 * 1000 << " us" << endl;
    
    cout << "----------------------------" << endl;
    uint64_t rdma_count = 0;
    uint64_t read_rdma_count = 0;
    uint64_t rdma_bandwidth = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      for (int j = 0; j < MAX_MACHINE; ++j) {
        rdma_count += rdma_cnt[i][j];
        rdma_bandwidth += rdma_bw[i][j];
      }
    }
    printf("Total RDMA count: %ld\n", rdma_count);
    printf("Total RDMA IOPS (MOPS): %lf\n", rdma_count / duration / 1000 / 1000);
    printf("Total RDMA BW (Gbps): %lf\n", rdma_bandwidth * 8 / duration / 1024 / 1024 / 1024);

    for(int i = 0; i < 6; i++){
        uint64_t node_rdma = 0;
        uint64_t node_bw = 0;
        for (int j = 0; j < MAX_APP_THREAD; ++j) {
            node_rdma += rdma_cnt[j][i];
            node_bw += rdma_bw[j][i];
        }
        printf("Node %d RDMA count: %ld\n", i, node_rdma);
        printf("Node %d RDMA IOPS (MOPS): %lf\n", i, node_rdma / duration / 1000 / 1000);
        printf("Node %d RDMA BW (Gbps): %lf\n", i, node_bw * 8 / duration / 1024 / 1024 / 1024);
    }
    cout << "----------------------------" << endl;

#ifdef SHERMAN_LATENCY
	int lat_count = 0;
	for(int i = 0; i < SHERMAN_MAX_LATENCY_SIZE; i++) {
		for(int k = 0; k < define::kMaxCoro; k++) {
		    for(int j = 0; j < MAX_APP_THREAD; j++) {
			    lat_count += dis_latency[j][k][i];
            }
		}
		if(lat_count > 0.50 * (double)((tran_ops / kComputeNodeCount /
		                                num_threads * num_threads))) {
			cout << "# P50 " << i * 100 << " ns" << endl;
			break;
		}
	}
    lat_count = 0;
	for(int i = 0; i < SHERMAN_MAX_LATENCY_SIZE; i++) {
		for(int k = 0; k < define::kMaxCoro; k++) {
		    for(int j = 0; j < MAX_APP_THREAD; j++) {
			    lat_count += dis_latency[j][k][i];
            }
		}
		if(lat_count > 0.99 * (double)((tran_ops / kComputeNodeCount /
		                                num_threads * num_threads))) {
			cout << "# P99 " << i * 100 << " ns" << endl;
			break;
		}
	}
#endif

    return 0;
}