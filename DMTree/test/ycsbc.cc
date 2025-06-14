#include "DMTree.h"
#include "util/timer.h"
#include "util/ycsb.h"
#include <future>

#define USE_CORO
#define DMTREE_LATENCY
#define DMTREE_MAX_LATENCY_SIZE 100000

std::atomic<bool> need_stop(false);

DMConfig config;
uint64_t num_threads = 0;
int kCoroCnt = 4;
int kNodeCount = 7;
int kComputeNodeCount = 6;
int kMemoryNodeCount = 1;

using namespace std;
using namespace sds;
using OpRecord = util::OpRecord;

DMVerbs* dmv;
DMTree* dmtree;

bool rehash_key_ = true;
util::WorkloadBuilder* builder_[MAX_THREAD_NUM];

#ifdef DMTREE_LATENCY
uint64_t exeTime[MAX_THREAD_NUM][define::kMaxCoro];
struct timespec time_start[MAX_THREAD_NUM][define::kMaxCoro];
struct timespec time_end[MAX_THREAD_NUM][define::kMaxCoro];

double request_latency[MAX_THREAD_NUM][define::kMaxCoro];
uint64_t dis_latency[MAX_THREAD_NUM][define::kMaxCoro][DMTREE_MAX_LATENCY_SIZE];
#endif

thread_local CoroCall worker[define::kMaxCoro];
thread_local CoroCall master;
thread_local CoroQueue busy_waiting_queue;
thread_local uint64_t thread_finish_ops;
thread_local uint64_t thread_finish_coros;
atomic<uint64_t> finish_thread_count;

atomic<uint64_t> load_count;
extern uint64_t rdma_cnt[MAX_THREAD_NUM][MAX_MACHINE_NUM];
extern uint64_t rdma_bw[MAX_THREAD_NUM][MAX_MACHINE_NUM];

void reset() {
	cout << "A part finished." << endl;
	for(int i = 0; i < MAX_THREAD_NUM; i++) {
		for(int j = 0; j < MAX_MACHINE_NUM; j++) {
			rdma_cnt[i][j] = 0;
			rdma_bw[i][j] = 0;
		}
	}
#ifdef DMTREE_LATENCY
	for(int i = 0; i < MAX_THREAD_NUM; i++) {
		for(int j = 0; j < define::kMaxCoro; j++) {
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
	if(str.size() < KEY_SIZE) {
		return std::string(KEY_SIZE - str.size(), '0') + str;
	} else {
		return str.substr(0, KEY_SIZE);
	}
}

void do_transaction() {

	int th_id = dmv->getMyThreadID();
	auto& builder = builder_[th_id];

#ifdef DMTREE_LATENCY
	clock_gettime(CLOCK_REALTIME, &time_start[th_id][0]);
#endif

	OpRecord record;
	builder->fill_record(record);

	auto key = build_key_str(record.key);
	int rc;
	if(record.type == util::INSERT) {
		Value value = 12;
		dmtree->write(str2key(key), value);
	}
	if(record.type == util::READ) {
		Value value;
		dmtree->read(str2key(key), value);
	}
	if(record.type == util::UPDATE) {
		Value value = 12;
		dmtree->write(str2key(key), value);
	}
	if(record.type == util::SCAN) {
		std::vector<std::string> vec;
		Value values[1000];
		dmtree->scan(str2key(key), KeyMax, record.scan_len);
	}
	if(record.type == util::READMODIFYWRITE) {
		Value value;
		value = 12;
		dmtree->read(str2key(key), value);
		dmtree->write(str2key(key), value);
	}

#ifdef DMTREE_LATENCY
	clock_gettime(CLOCK_REALTIME, &time_end[th_id][0]);
	exeTime[th_id][0] =
	    (time_end[th_id][0].tv_sec - time_start[th_id][0].tv_sec) * 1000000000 +
	    (time_end[th_id][0].tv_nsec - time_start[th_id][0].tv_nsec);
	request_latency[th_id][0] += exeTime[th_id][0];
	if(exeTime[th_id][0] / 100 >= DMTREE_MAX_LATENCY_SIZE) {
		cout << "long time " << exeTime[th_id][0] << " ns" << endl;
	} else {
		dis_latency[th_id][0][exeTime[th_id][0] / 100]++;
	}
#endif
}

void coro_worker(CoroYield& yield, uint64_t operation_count,
                 uint64_t thread_count, int coro_id) {
	CoroContext ctx;
	ctx.coro_id = coro_id;
	ctx.master = &master;
	ctx.yield = &yield;
	ctx.busy_waiting_queue = &busy_waiting_queue;

	int th_id = dmv->getMyThreadID();
	auto& builder = builder_[th_id];

	while(thread_finish_ops < operation_count) {

#ifdef DMTREE_LATENCY
		clock_gettime(CLOCK_REALTIME, &time_start[th_id][coro_id]);
#endif

		OpRecord record;
		builder->fill_record(record);

		auto key = build_key_str(record.key);
		int rc;
		if(record.type == util::INSERT) {
			Value value = 12;
			dmtree->write(str2key(key), value, coro_id, &ctx);
		}
		if(record.type == util::READ) {
			Value value;
			dmtree->read(str2key(key), value, coro_id, &ctx);
		}
		if(record.type == util::UPDATE) {
			Value value = 12;
			dmtree->write(str2key(key), value, coro_id, &ctx);
		}
		if(record.type == util::SCAN) {
			std::vector<std::string> vec;
			Value values[1000];
			dmtree->scan(str2key(key), KeyMax, record.scan_len, coro_id, &ctx);
		}
		if(record.type == util::READMODIFYWRITE) {
			Value value;
			value = 12;
			dmtree->read(str2key(key), value, coro_id, &ctx);
			dmtree->write(str2key(key), value, coro_id, &ctx);
		}

#ifdef DMTREE_LATENCY
		clock_gettime(CLOCK_REALTIME, &time_end[th_id][coro_id]);
		exeTime[th_id][coro_id] = (time_end[th_id][coro_id].tv_sec -
		                           time_start[th_id][coro_id].tv_sec) *
		                              1000000000 +
		                          (time_end[th_id][coro_id].tv_nsec -
		                           time_start[th_id][coro_id].tv_nsec);
		request_latency[th_id][coro_id] += exeTime[th_id][coro_id];
		if(exeTime[th_id][coro_id] / 100 >= DMTREE_MAX_LATENCY_SIZE) {
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
	}

	if(finish_thread_count.load() == thread_count) {
		need_stop.store(true, std::memory_order_release);
		std::cout << "now need stop" << std::endl;
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
		int res = dmv->poll_rdma_cqs(wc);

		if(need_stop.load(std::memory_order_acquire)) {
			break;
		}
		for(int i = 0; i < res; i++) {
			yield(worker[wc[i].wr_id]);
			if(need_stop.load(std::memory_order_acquire)) {
				break;
			}
		}
		if(!busy_waiting_queue.empty()) {
			auto next = busy_waiting_queue.front();
			busy_waiting_queue.pop();
			auto next_coro = next.first;
			if(next.second()) {
				yield(worker[next_coro]);
			} else {
				busy_waiting_queue.push(next);
			}
		}
	}

	for(int i = 0; i < coro_cnt; ++i) {
		yield(worker[i]);
	}
}

void run_coroutine(uint64_t operation_count, uint64_t thread_count,
                   int coro_cnt) {

	thread_finish_ops = 0;
	thread_finish_coros = 0;
	std::cout << "thread start " << dmv->getMyThreadID() << ", num "
	          << operation_count << std::endl;

	using namespace std::placeholders;
	assert(coro_cnt <= define::kMaxCoro);
	for(int i = 0; i < coro_cnt; ++i) {
		worker[i] = CoroCall(std::bind(&coro_worker, std::placeholders::_1,
		                               operation_count, thread_count, i));
	}
	master = CoroCall(std::bind(&coro_master, std::placeholders::_1, coro_cnt));

	master();

	std::cout << "thread finish " << dmv->getMyThreadID() << ", num "
	          << thread_finish_ops << std::endl;
}

uint64_t thread_load(uint64_t start, uint64_t num_ops) {

	dmv->registerThread();
	uint64_t count = 0;
	cout << "load range: [" << start << "," << start + num_ops << "]"
	     << std::endl;

	for(uint64_t i = 0; i < num_ops; ++i) {
		if(load_count++ % 100000 == 0) {
			printf("load %d times\n", load_count.load());
		}
		auto key = build_key_str(start + i);
		Value value = 12;
		dmtree->write(str2key(key), value);
		count++;
	}
	return count;
}

uint64_t thread_warm(uint64_t start, uint64_t num_ops) {

	dmv->registerThread();
	uint64_t count = 0;
	cout << "warm range: [" << start << "," << start + num_ops << "]"
	     << std::endl;

	for(uint64_t i = 0; i < num_ops; ++i) {
		auto key = build_key_str(start + i);
		Value value;
		dmtree->read(str2key(key), value);
		count++;
	}
	return count;
}

uint64_t thread_run(const uint64_t num_ops, const uint64_t tran_ops) {

	dmv->registerThread();
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

int main(const int argc, const char* argv[]) {

	load_count.store(0);
	finish_thread_count.store(0);

	config.machineNR = kNodeCount;
	config.ComputeNumber = kComputeNodeCount;
	config.MemoryNumber = kMemoryNodeCount;

	dmv = DMVerbs::getInstance(config);
	dmv->registerThread();
	dmtree = new DMTree(dmv);

	cout << "creating benchmark" << endl;
	num_threads = stoi(argv[1]);
	kCoroCnt = stoi(argv[2]);
	string worloads = argv[3];
	string distribution = argv[4];

	dmv->resetThread();
	vector<future<uint64_t>> actual_ops;
	uint64_t perload_ops = 1000000000;
	int tran_ops = 100000000;
	int sum = 0;

	for(int i = 0; i < num_threads; ++i) {
		uint64_t insert_start =
		    perload_ops + (dmv->getMyNodeID() % config.ComputeNumber) *
		                      (tran_ops / config.ComputeNumber);
		builder_[i] = util::WorkloadBuilder::Create(
		    worloads.c_str(), distribution, perload_ops, insert_start, 0.99);
		assert(builder_[i]);
	}
	cout << "creating benchmark finish" << endl;
	dmv->barrier("init", config.ComputeNumber);

	// per-load key-value entries
	num_threads = 72;
	uint64_t start = (dmv->getMyNodeID() % config.ComputeNumber) *
	                 (perload_ops / config.ComputeNumber);
	uint64_t thread_op = perload_ops / config.ComputeNumber / num_threads;
	for(int i = 0; i < num_threads; ++i) {
		sleep(1);
		if(i == (num_threads - 1)) {
			thread_op += (perload_ops % (config.ComputeNumber * num_threads));
			thread_op += config.ComputeNumber;
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

	dmv->barrier("loading", config.ComputeNumber);
	dmv->resetThread();
	reset();

	// read key-value entries to fill up the cache
	actual_ops.clear();
	start = 0;
	thread_op = perload_ops / num_threads;

	for(int i = 0; i < num_threads; ++i) {
		if(i == (num_threads - 1)) {
			thread_op += (perload_ops % num_threads);
			thread_op += config.ComputeNumber;
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
	dmv->barrier("running", config.ComputeNumber);
	dmv->resetThread();

	// perform transactions, time between timer.begin() and timer.end().
	num_threads = stoi(argv[1]);
	actual_ops.clear();
	utils::Timer<double> timer;
	timer.Start();
	for(int i = 0; i < num_threads; ++i) {
		actual_ops.emplace_back(
		    async(launch::async, thread_run,
		          tran_ops / config.ComputeNumber / num_threads, tran_ops));
	}
	assert((int)actual_ops.size() == num_threads);

	sum = 0;
	for(auto& n : actual_ops) {
		assert(n.valid());
		sum += n.get();
	}
	double duration = timer.End();

	// Operations after transactions are excluded from performance stats.
	dmtree->print_cache_info();

	dmv->barrier("finish", config.ComputeNumber);

	cout << "----------------------------" << endl;
	cout << "Number of Thread: " << num_threads << endl;

#ifdef USE_CORO
	cout << "Number of OutStanding WR: " << kCoroCnt << endl;
#endif
	cout << "# Transaction throughput (MOPS): "
	     << (tran_ops / config.ComputeNumber / num_threads * num_threads) /
	            duration / 1000 / 1000
	     << endl;
	cout << "Total Time: " << duration << "s" << endl;
	cout << "Latency: "
	     << duration /
	            (tran_ops / config.ComputeNumber / num_threads * num_threads) *
	            1000 * 1000
	     << " us" << endl;
	uint64_t rdma_count = 0;
	uint64_t rdma_bandwidth = 0;
	for(int i = 0; i < MAX_THREAD_NUM; ++i) {
		for(int j = 0; j < MAX_MACHINE_NUM; j++) {
			rdma_count += rdma_cnt[i][j];
			rdma_bandwidth += rdma_bw[i][j];
		}
	}
	printf("Total RDMA count: %ld\n", rdma_count);
	printf("Total RDMA IOPS (MOPS): %lf\n",
	       rdma_count / duration / 1000 / 1000);
	printf("Total RDMA BW (Gbps): %lf\n",
	       rdma_bandwidth * 8 / duration / 1024 / 1024 / 1024);

	for(int i = 0; i < MAX_MACHINE_NUM; i++) {
		uint64_t node_rdma = 0;
		uint64_t node_bw = 0;
		for(int j = 0; j < MAX_THREAD_NUM; ++j) {
			node_rdma += rdma_cnt[j][i];
			node_bw += rdma_bw[j][i];
		}
		printf("Node %d RDMA count: %ld\n", i, node_rdma);
		printf("Node %d RDMA IOPS (MOPS): %lf\n", i,
		       node_rdma / duration / 1000 / 1000);
		printf("Node %d RDMA BW (Gbps): %lf\n", i,
		       node_bw * 8 / duration / 1024 / 1024 / 1024);
	}
	cout << "----------------------------" << endl;

#ifdef DMTREE_LATENCY
	int lat_count = 0;
	for(int i = 0; i < DMTREE_MAX_LATENCY_SIZE; i++) {
		for(int k = 0; k < define::kMaxCoro; k++) {
			for(int j = 0; j < MAX_THREAD_NUM; j++) {
				lat_count += dis_latency[j][k][i];
			}
		}
		if(lat_count > 0.50 * (double)((tran_ops / config.ComputeNumber /
		                                num_threads * num_threads))) {
			cout << "# P50 " << i * 100 << " ns" << endl;
			break;
		}
	}
	lat_count = 0;
	for(int i = 0; i < DMTREE_MAX_LATENCY_SIZE; i++) {
		for(int k = 0; k < define::kMaxCoro; k++) {
			for(int j = 0; j < MAX_THREAD_NUM; j++) {
				lat_count += dis_latency[j][k][i];
			}
		}
		if(lat_count > 0.99 * (double)((tran_ops / config.ComputeNumber /
		                                num_threads * num_threads))) {
			cout << "# P99 " << i * 100 << " ns" << endl;
			break;
		}
	}
#endif
	return 0;
}