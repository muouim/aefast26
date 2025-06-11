#include "DMTree.h"
#include "util/timer.h"
#include <atomic>
#include <future>
#include <gperftools/profiler.h>

// #define USE_CORO
#define DMTREE_LATENCY
#define DMTREE_MAX_LATENCY_SIZE 100000

using namespace std;

uint64_t num_threads = 0;
uint64_t owr_count = 0;
uint64_t data_size = 0;

int kNodeCount = 2;
int kComputeNodeCount = 1;
int kMemoryNodeCount = 1;

DMVerbs* dmv;

#ifdef DMTREE_LATENCY
thread_local uint64_t request_time;
thread_local uint64_t request_cnt;
thread_local uint64_t post_send_time;
thread_local uint64_t post_send_cnt;
thread_local uint64_t poll_cq_time;
thread_local uint64_t poll_cq_cnt;
atomic<int> all_request_time;
atomic<int> all_request_cnt;
atomic<int> all_post_time;
atomic<int> all_poll_time;
atomic<int> all_post_cnt;
atomic<int> all_poll_cnt;
#endif

thread_local CoroCall worker[define::kMaxCoro];
thread_local CoroCall master;
thread_local CoroQueue busy_waiting_queue;

thread_local uint64_t thread_finish_ops;
thread_local uint64_t thread_finish_coros;
atomic<uint64_t> finish_thread_count;

thread_local uint64_t thread_finish_io;
atomic<int> finish_bandwith;

bool need_stop = false;

void reset() {
	cout << "A part finished." << endl;
#ifdef DMTREE_LATENCY
	all_request_time.store(0);
	all_request_cnt.store(0);
	all_post_time.store(0);
	all_poll_time.store(0);
	all_post_cnt.store(0);
	all_poll_cnt.store(0);
#endif
}

void coro_worker(CoroYield& yield, uint64_t operation_count,
                 uint64_t thread_count, int coro_id) {
	CoroContext ctx;
	ctx.coro_id = coro_id;
	ctx.master = &master;
	ctx.yield = &yield;
	ctx.busy_waiting_queue = &busy_waiting_queue;

	std::uniform_int_distribution<uint64_t> dist(
	    0, 20 * define::GB / num_threads / data_size - 1);
	std::mt19937 rnd;

	// RdmaOpRegion* rs = new RdmaOpRegion[2];

	while(thread_finish_ops < operation_count) {
		// total_finish_count++;
		Gaddr addr(0, 20 * define::GB / num_threads * dmv->getMyThreadID() +
		                  data_size * (dist(rnd)));
		auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();

		/*
		rs[0].source = (uint64_t)buffer;
		rs[0].dest = addr;
		rs[0].size = data_size;
		rs[0].is_on_chip = false;

		rs[1].source = (uint64_t)buffer + data_size;
		rs[1].dest = addr;
		rs[1].size = 8;
		rs[1].is_on_chip = false;

		dmv->read_batch(rs, 2, true, &ctx);
		*/
		struct timespec time_start;
		struct timespec time_end;

		clock_gettime(CLOCK_REALTIME, &time_start);

		dmv->read(buffer, addr, data_size, true, &ctx);

		clock_gettime(CLOCK_REALTIME, &time_end);
		post_send_time += (time_end.tv_sec - time_start.tv_sec) * 1000000000 +
		                  (time_end.tv_nsec - time_start.tv_nsec);
		post_send_cnt++;

		thread_finish_ops++;
		thread_finish_io += data_size;
		// thread_finish_io += 8;
	}

	thread_finish_coros++;
	if(thread_finish_coros == owr_count) {
		finish_thread_count++;
	}
	if(finish_thread_count.load() == thread_count) {
		need_stop = true;
	}

	while(!need_stop) {
		yield(master);
	}
	yield(master);
}

void coro_master(CoroYield& yield, int coro_cnt) {

	for(int i = 0; i < coro_cnt; ++i) {
		yield(worker[i]);
	}

	while(!need_stop) {
		ibv_wc wc[define::kMaxCoro];
		struct timespec time_start;
		struct timespec time_end;

		clock_gettime(CLOCK_REALTIME, &time_start);

		int res = dmv->poll_rdma_cqs(wc);
		while(res == 0 && !need_stop) {
			res = dmv->poll_rdma_cqs(wc);
		}
		for(int i = 0; i < res; i++) {
			yield(worker[wc[i].wr_id]);
		}

		clock_gettime(CLOCK_REALTIME, &time_end);
		poll_cq_time += (time_end.tv_sec - time_start.tv_sec) * 1000000000 +
		                (time_end.tv_nsec - time_start.tv_nsec);
		poll_cq_cnt++;
	}
}

void run_coroutine(uint64_t operation_count, uint64_t thread_count,
                   int coro_cnt) {

	thread_finish_ops = 0;
	thread_finish_io = 0;
	thread_finish_coros = 0;

	// std::cout << "thread start " << dmv->getMyThreadID() << ", num "
	//           << operation_count << std::endl;

	using namespace std::placeholders;
	assert(coro_cnt <= define::kMaxCoro);
	for(int i = 0; i < coro_cnt; ++i) {
		worker[i] = CoroCall(std::bind(&coro_worker, std::placeholders::_1,
		                               operation_count, thread_count, i));
	}
	master = CoroCall(std::bind(&coro_master, std::placeholders::_1, coro_cnt));

	master();
	finish_bandwith.fetch_add((double)thread_finish_io / (double)1024 /
	                          (double)1024);
	// std::cout << "thread finish " << dmv->getMyThreadID() << ", num "
	//           << thread_finish_ops << std::endl;
}

uint64_t thread_run(const uint64_t num_ops, const uint64_t total_ops) {
	request_time = 0;
	request_cnt = 0;
	post_send_time = 0;
	post_send_cnt = 0;
	poll_cq_time = 0;
	poll_cq_cnt = 0;

	dmv->registerThread();
	uint64_t count = 0;

	uint64_t cq_count = 0;
    // int batch_cq_count = 0;
#ifdef USE_CORO
	run_coroutine(num_ops, num_threads, owr_count);
	count += num_ops;
#else
	std::uniform_int_distribution<uint64_t> dist(
	    0, 20 * define::GB / num_threads / data_size - 1);

	// RdmaOpRegion* rs = new RdmaOpRegion[owr_count];
	std::mt19937 rnd;

	struct timespec request_time_start[define::kMaxCoro];
	struct timespec request_time_end[define::kMaxCoro];

	for(uint64_t i = 0; i < num_ops; ++i) {
		Gaddr addr(0, 20 * define::GB / num_threads * dmv->getMyThreadID() +
		                  data_size * (dist(rnd)));
        addr.offset = 0;
        addr.nodeID = thread_finish_ops % kMemoryNodeCount;
		auto buffer = (dmv->get_rbuf(cq_count)).get_page_buffer();
		struct timespec time_start;
		struct timespec time_end;

		clock_gettime(CLOCK_REALTIME, &time_start);
		clock_gettime(CLOCK_REALTIME, &request_time_start[cq_count]);

        /*
		// std::cout<<"run "<<i<<std::endl;
		rs[cq_count].source = (uint64_t)buffer;
		rs[cq_count].dest = addr;
		rs[cq_count].size = data_size;
		rs[cq_count].is_on_chip = true;
		cq_count++;
		thread_finish_io += data_size;

		if(cq_count == owr_count) {
			clock_gettime(CLOCK_REALTIME, &time_start);
			
            dmv->read_batch(rs, cq_count);
            
            clock_gettime(CLOCK_REALTIME, &time_end);
			post_send_time +=
			    (time_end.tv_sec - time_start.tv_sec) * 1000000000 +
			    (time_end.tv_nsec - time_start.tv_nsec);
			post_send_cnt++;

		    clock_gettime(CLOCK_REALTIME, &time_start);
            batch_cq_count++;

            if(batch_cq_count == 8) {
                dmv->poll_rdma_cq(8);
                batch_cq_count = 0;
            }
            
			cq_count = 0;

		    clock_gettime(CLOCK_REALTIME, &time_end);
		    for(int i = 0; i < owr_count; i++) {
		        clock_gettime(CLOCK_REALTIME, &request_time_end[i]);
		        request_time += (request_time_end[i].tv_sec -
		                         request_time_start[i].tv_sec) *
		                            1000000000 +
		                        (request_time_end[i].tv_nsec -
		                         request_time_start[i].tv_nsec);
		        request_cnt++;
		    }
		    poll_cq_time += (time_end.tv_sec - time_start.tv_sec) * 1000000000 +
		                    (time_end.tv_nsec - time_start.tv_nsec);
		    poll_cq_cnt++;
		}
        */

		dmv->read(buffer, addr, data_size);

		clock_gettime(CLOCK_REALTIME, &time_end);
		post_send_time += (time_end.tv_sec - time_start.tv_sec) * 1000000000 +
		                  (time_end.tv_nsec - time_start.tv_nsec);
		post_send_cnt++;

		cq_count++;
		thread_finish_io += data_size;

		if(cq_count == owr_count) {
		    clock_gettime(CLOCK_REALTIME, &time_start);

		    // dmv->poll_rdma_cq(cq_count);
		    int res = 0;
		    while(res < cq_count) {
		        ibv_wc wc[define::kMaxCoro];
		        res += dmv->poll_rdma_cqs(wc);
		    }
		    cq_count = 0;

		    clock_gettime(CLOCK_REALTIME, &time_end);
		    for(int i = 0; i < owr_count; i++) {
		        clock_gettime(CLOCK_REALTIME, &request_time_end[i]);
		        request_time += (request_time_end[i].tv_sec -
		                         request_time_start[i].tv_sec) *
		                            1000000000 +
		                        (request_time_end[i].tv_nsec -
		                         request_time_start[i].tv_nsec);
		        request_cnt++;
		    }
		    poll_cq_time += (time_end.tv_sec - time_start.tv_sec) * 1000000000 +
		                    (time_end.tv_nsec - time_start.tv_nsec);
		    poll_cq_cnt++;
		}
		count++;
	}

	if(cq_count != 0) {
		dmv->poll_rdma_cq(cq_count);
	}
    /*
	if(batch_cq_count != 0) {
		dmv->poll_rdma_cq(batch_cq_count);
	}
    */
	finish_bandwith.fetch_add((double)thread_finish_io / (double)1024 /
	                          (double)1024);
#endif
	all_request_time.fetch_add(request_time / 100000);
	all_request_cnt.fetch_add(request_cnt);
	all_post_time.fetch_add(post_send_time / 100000);
	all_post_cnt.fetch_add(post_send_cnt);
	all_poll_time.fetch_add(poll_cq_time / 100000);
	all_poll_cnt.fetch_add(poll_cq_cnt);

	assert(count == num_ops);
	return count;
}

int main(const int argc, const char* argv[]) {

	DMConfig config;
	config.machineNR = kNodeCount;
	config.ComputeNumber = kComputeNodeCount;
	config.MemoryNumber = kMemoryNodeCount;
	dmv = DMVerbs::getInstance(config);

	dmv->registerThread();

    int loop_count = 0;

	for(int s = 3; s <= 3; s++)
		for(int i = 8; i < 80; i += 6) {
			for(int j = 4; j < 5; j *= 2) {
                
                loop_count++;
                
				reset();
				need_stop = false;
				finish_thread_count.store(0);
				finish_bandwith.store(0);
				// total_finish_count.store(0);

				dmv->resetThread();
            
				num_threads = i;
				owr_count = j;
				data_size = pow(2, s);

				// per-load key-value entries
				uint64_t total_ops = 10000000;
				int sum = 0;

				vector<future<uint64_t>> actual_ops;

				// node_0 load key-value entries
				if(dmv->getMyNodeID() == 0 + config.MemoryNumber) {
					std::cout << "i am compute node 0" << std::endl;
				}

				utils::Timer<double> timer;
				timer.Start();

				std::cout << "running " << std::endl;
	            ProfilerStart("my.prof");

				for(int i = 0; i < num_threads; ++i) {
					actual_ops.emplace_back(async(launch::async, thread_run,
					                              total_ops / num_threads,
					                              total_ops));
				}
				assert((int)actual_ops.size() == num_threads);

				sum = 0;
				for(auto& n : actual_ops) {
					assert(n.valid());
					sum += n.get();
				}
				double duration = timer.End();
	            ProfilerStop();

				// cout << "Total Finished: " << total_finish_count.load() <<
				// endl;
				cout << "Data Size: " << data_size << endl;
				cout << "Number of Thread: " << num_threads << endl;
				cout << "Number of OutStanding WR: " << owr_count << endl;

				cout << "# Transaction throughput (MOPS): "
				     << total_ops / duration / 1000 / 1000 << endl;

				cout << "# Bandwith (Gbps): "
				     << finish_bandwith.load() / (double)duration /
				            (double)1024 * 8
				     << endl;

				cout << "Total Time: " << duration << "s" << endl;
				cout << "Latency: " << duration / total_ops * 1000 * 1000
				     << " us" << endl;

				cout << "request time: "
				     << (double)all_request_time.load() /
				            (double)all_request_cnt.load() * 100000
				     << " ns, count: " << all_request_cnt.load() << endl;
				cout << "post_send time: "
				     << (double)all_post_time.load() /
				            (double)all_post_cnt.load() * 100000
				     << " ns, count: " << all_post_cnt.load() << endl;
				cout << "poll cq time: "
				     << (double)all_poll_time.load() /
				            (double)all_poll_cnt.load() * 100000
				     << " ns, count: " << all_poll_cnt.load() << endl;

				cout << "----------------------------" << endl;
			}
		}

	return 0;
}