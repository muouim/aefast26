#include "Tree.h"
#include "Timer.h"
#include <city.h>

#include <stdlib.h>
#include <thread>
#include <time.h>
#include <vector>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <future>
#include <unistd.h>
#include <gperftools/profiler.h>

#include "Timer.h"
#include "util/ycsb.h"
#include "util/timer.h"


#define USE_CORO
#define SHERMAN_LATENCY
#define SHERMAN_MAX_LATENCY_SIZE 100000

extern bool need_stop;

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

extern double cache_miss[MAX_APP_THREAD];
extern double cache_hit[MAX_APP_THREAD];
extern uint64_t rdma_cnt[MAX_APP_THREAD][MAX_MACHINE];
extern uint64_t rdma_bw[MAX_APP_THREAD][MAX_MACHINE];

#ifdef SHERMAN_LATENCY
uint64_t exeTime[MAX_APP_THREAD][MAX_CORO_NUM];
struct timespec time_start[MAX_APP_THREAD][MAX_CORO_NUM];
struct timespec time_end[MAX_APP_THREAD][MAX_CORO_NUM];

double request_latency[MAX_APP_THREAD][MAX_CORO_NUM];
uint64_t dis_latency[MAX_APP_THREAD][MAX_CORO_NUM][SHERMAN_MAX_LATENCY_SIZE];
#endif

using WorkFunc = std::function<void (Tree *, const Request&, CoroPull *)>;

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
	    for(int j = 0; j < MAX_CORO_NUM; j++) {
            request_latency[i][j] = 0;
            memset(dis_latency[i][j], 0, sizeof(dis_latency[i][j]));
        }
	}
#endif
}

std::string build_key_str(uint64_t key) {
    if(rehash_key_) {
        key = util::FNVHash64(key);
    }
    auto str = std::to_string(key);
    if(str.size() < define::keyLen) {
        return std::string(define::keyLen - str.size(), '0') + str;
    } else {
        return str.substr(0, define::keyLen);
    }
}

void work_func(Tree *tree, const Request& r, CoroPull *sink) {
  if (r.req_type == SEARCH) {
    Value v;
    tree->search(r.k, v, sink);
  }
  else if (r.req_type == INSERT) {
    tree->insert(r.k, r.v, sink);
  }
  else if (r.req_type == UPDATE) {
    tree->update(r.k, r.v, sink);
  }
  else {
    std::map<Key, Value> ret;
    tree->range_query(r.k, r.k + r.range_size, ret);
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
        tree->search(str2key(key), value);
    }
    if(record.type == util::UPDATE) {
        Value value = 12;
        tree->update(str2key(key), value);
    }
    if(record.type == util::SCAN) {
        std::vector<std::string> vec;
        std::map<Key, Value> values;
        // std::cout<<"-----------------------------------------------"<<std::endl;
        // std::cout<<"scan form: "<<record.key <<", to "<< record.key + record.scan_len<<std::endl;
        auto to = build_key_str(record.key + record.scan_len);
        tree->range_query(str2key(key), str2key(to), values);
        if(values.size() < record.scan_len) {
            std::cout<<"scan len less: "<<values.size()<<" "<<record.scan_len<<std::endl;
        }
        // std::cout<<"-----------------------------------------------"<<std::endl;
    }
    if(record.type == util::READMODIFYWRITE) {
        Value value;
        value = 12;
        tree->search(str2key(key), value);
        tree->update(str2key(key), value);
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

void coro_worker(CoroPull &sink, WorkFunc work_func, uint64_t operation_count, uint64_t thread_count, int coro_id) {

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
            tree->insert(str2key(key), value, &sink);
        }
        if(record.type == util::READ) {
            Value value;
            if(!tree->search(str2key(key), value, &sink)) {
                std::cout<<"No Find Key: "<<key<<std::endl;
            }
        }
        if(record.type == util::UPDATE) {
            Value value = 12;
            tree->update(str2key(key), value, &sink);
        }
        if(record.type == util::SCAN) {
            std::vector<std::string> vec;
            std::map<Key, Value> values;
            auto to = build_key_str(record.key + record.scan_len);
            tree->range_query(str2key(key), str2key(to), values);
        }
        if(record.type == util::READMODIFYWRITE) {
            Value value;
            value = 12;
            tree->search(str2key(key), value, &sink);
            tree->update(str2key(key), value, &sink);
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
        
        tree->busy_waiting_queue.push(sink.get());
        sink();
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
        need_stop = true;
    }
    tree->busy_waiting_queue.push(sink.get());
    sink();
}

void run_coroutine(WorkFunc work_func, uint64_t operation_count, uint64_t thread_count, int coro_cnt) {

    thread_finish_ops = 0;
    thread_finish_coros = 0;
    std::cout << "thread start " << dsm->getMyThreadID() << ", num "
              << operation_count << std::endl;

    using namespace std::placeholders;
    assert(coro_cnt <= MAX_CORO_NUM);
    // define coroutines
    for (int i = 0; i < coro_cnt; ++i) {
        tree->workers.emplace_back([=](CoroPull& sink) {
        coro_worker(sink, work_func, operation_count, thread_count, i);
        });
    }
    // start running coroutines
    for (int i = 0; i < coro_cnt; ++i) {
        tree->workers[i](i);
    }
    while (!need_stop) {
        uint64_t next_coro_id;

        if (dsm->poll_rdma_cq_once(next_coro_id)) {
        tree->workers[next_coro_id](next_coro_id);
        }
        if (!tree->busy_waiting_queue.empty()) {
        auto next_coro_id = tree->busy_waiting_queue.front();
        tree->busy_waiting_queue.pop();
        tree->workers[next_coro_id](next_coro_id);
        }
    }

    std::cout << "thread finish total " << dsm->getMyThreadID() << ", num "
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
        if(load_count++ % 100000 == 0) {
            printf("warm %d times\n", load_count.load());
        }
		auto key = build_key_str(start + i);
        Value value;
        if(!tree->search(str2key(key), value)) {
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
    run_coroutine(work_func, num_ops, num_threads, kCoroCnt);
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

int main(const int argc, const char* argv[]) {
    
    need_stop = false;
    
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

	uint64_t tran_ops = 100000000;
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

    // tree->statistics();
    dsm->barrier("loading", kComputeNodeCount);
    dsm->resetThread();
    tree->clear_debug_info();

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

    ProfilerStart("my.prof");

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
    ProfilerStop();

	dsm->barrier("finish", kComputeNodeCount);

    cout << "Number of Thread: " << num_threads << endl;

#ifdef USE_CORO
    cout << "Number of OutStanding WR: " << kCoroCnt << endl;
#endif
    cout << "# Transaction throughput (MOPS): "
         << (tran_ops / kComputeNodeCount / num_threads * num_threads) / duration / 1000 / 1000 << endl;
    cout << "Total Time: " << duration << "s" << endl;
    cout << "Latency: " << duration / (tran_ops / kComputeNodeCount / num_threads * num_threads) * 1000 * 1000 << " us" << endl;
    
    cout << "----------------------------" << endl;
    double all = 0, hit = 0;
    uint64_t rdma_count = 0;
    uint64_t read_rdma_count = 0;
    uint64_t rdma_bandwidth = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      all += (cache_hit[i] + cache_miss[i]);
      hit += cache_hit[i];
      for (int j = 0; j < MAX_MACHINE; ++j) {
        rdma_count += rdma_cnt[i][j];
        rdma_bandwidth += rdma_bw[i][j];
      }
    }
    printf("cache hit rate: %lf\n", hit * 1.0 / all);
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

    tree->statistics();

#ifdef SHERMAN_LATENCY
	int lat_count = 0;
	for(int i = 0; i < SHERMAN_MAX_LATENCY_SIZE; i++) {
		for(int k = 0; k < MAX_CORO_NUM; k++) {
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
		for(int k = 0; k < MAX_CORO_NUM; k++) {
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