#include "Rolex.h"
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

#include "Timer.h"
#include "util/ycsb.h"
#include "util/timer.h"

using namespace std;
using namespace sds;

bool rehash_key_ = false;
util2::WorkloadBuilder* builder_[MAX_APP_THREAD];


#ifdef LONG_TEST_EPOCH
  #define TEST_EPOCH 40
  #define TIME_INTERVAL 1
#else
#ifdef SHORT_TEST_EPOCH
  #define TEST_EPOCH 5
  #define TIME_INTERVAL 0.2
#else
#ifdef MIDDLE_TEST_EPOCH
  #define TEST_EPOCH 10
  #define TIME_INTERVAL 1
#else
  #define TEST_EPOCH 10
  #define TIME_INTERVAL 0.5
#endif
#endif
#endif

#define MAX_THREAD_REQUEST 10000000
#define LOAD_HEARTBEAT 100000
// #define USE_CORO
#define EPOCH_LAT_TEST
#define LOADER_NUM 8 // [CONFIG] 8

extern uint64_t lock_fail[MAX_APP_THREAD];
extern uint64_t write_handover_num[MAX_APP_THREAD];
extern uint64_t try_write_op[MAX_APP_THREAD];
extern uint64_t read_handover_num[MAX_APP_THREAD];
extern uint64_t try_read_op[MAX_APP_THREAD];
extern uint64_t read_leaf_retry[MAX_APP_THREAD];
extern uint64_t leaf_read_syn[MAX_APP_THREAD];
extern uint64_t try_read_leaf[MAX_APP_THREAD];
extern double load_factor_sum[MAX_APP_THREAD];
extern uint64_t split_hopscotch[MAX_APP_THREAD];
extern uint64_t correct_speculative_read[MAX_APP_THREAD];
extern uint64_t try_speculative_read[MAX_APP_THREAD];
extern uint64_t want_speculative_read[MAX_APP_THREAD];
extern std::map<uint64_t, uint64_t> range_cnt[MAX_APP_THREAD];

int kThreadCount;
int kNodeCount;
int kCoroCnt = 8;
bool kIsStr;
bool kIsScan;
bool kIsInsert;
#ifdef USE_CORO
bool kUseCoro = true;
#else
bool kUseCoro = false;
#endif

std::string ycsb_load_path;
std::string ycsb_trans_path;
int fix_range_size = -1;


std::thread th[MAX_APP_THREAD];
uint64_t tp[MAX_APP_THREAD][MAX_CORO_NUM];

extern volatile bool need_stop;
extern volatile bool need_clear[MAX_APP_THREAD];
extern uint64_t latency[MAX_APP_THREAD][MAX_CORO_NUM][LATENCY_WINDOWS];
uint64_t latency_th_all[LATENCY_WINDOWS];

std::default_random_engine e;
std::uniform_int_distribution<Value> randval(define::kValueMin, define::kValueMax);

RolexIndex *rolex_index;
DSM *dsm;
std::vector<Key> train_keys;


class RequsetGenBench : public RequstGen {
public:
  RequsetGenBench(DSM* dsm, Request* req, int req_num, int coro_id, int coro_cnt) :
                  dsm(dsm), req(req), req_num(req_num), coro_id(coro_id), coro_cnt(coro_cnt) {
    local_thread_id = dsm->getMyThreadID();
    cur = coro_id;
    epoch_id = 0;
    extra_k = MAX_KEY_SPACE_SIZE + kThreadCount * kCoroCnt * dsm->getMyNodeID() + local_thread_id * kCoroCnt + coro_id;
    flag = false;
  }

  Request next() override {
    cur = (cur + coro_cnt) % req_num;
    if (req[cur].req_type == INSERT) {
      if (cur + coro_cnt >= req_num) {
        // need_stop = true;
        ++ epoch_id;
        flag = true;
      }
      if (kIsStr) {
        req[cur].k = req[cur].k + epoch_id;   // For insert workloads, key should remain nonexist
      }
      else if (flag) {
        req[cur].k = int2key(extra_k);
        extra_k += kThreadCount * kCoroCnt * dsm->getClusterSize();
      }
    }
    tp[local_thread_id][coro_id]++;
    req[cur].v = randval(e);  // make value different per-epoch
    return req[cur];
  }

private:
  DSM *dsm;
  Request* req;
  int req_num;
  int coro_id;
  int coro_cnt;
  int local_thread_id;
  int cur;
  uint8_t epoch_id;
  uint64_t extra_k;
  bool flag;
};


RequstGen *gen_func(DSM* dsm, Request* req, int req_num, int coro_id, int coro_cnt) {
  return new RequsetGenBench(dsm, req, req_num, coro_id, coro_cnt);
}


void work_func(RolexIndex *rolex_index, const Request& r, CoroPull *sink) {
  if (r.req_type == SEARCH) {
    Value v;
    rolex_index->search(r.k, v, sink);
  }
  else if (r.req_type == INSERT) {
    rolex_index->insert(r.k, r.v, sink);
  }
  else if (r.req_type == UPDATE) {
    rolex_index->update(r.k, r.v, sink);
  }
  else {
    std::map<Key, Value> ret;
    rolex_index->range_query(r.k, r.k + r.range_size, ret);
  }
}


rolex::Timer bench_timer;
std::atomic<int64_t> warmup_cnt{0};
std::atomic_bool ready{false};


void thread_load(int id) {
  // use LOADER_NUM threads to load ycsb
  uint64_t loader_id = std::min(kThreadCount, LOADER_NUM) * dsm->getMyNodeID() + id;

  printf("I am loader %lu\n", loader_id);

  // 1. insert ycsb_load
  std::string op;
  std::ifstream load_in(ycsb_load_path + std::to_string(loader_id));
  if (!load_in.is_open()) {
    printf("Error opening load file\n");
    assert(false);
  }
  Key k;
  int cnt = 0;
  if (!kIsStr) {  // int workloads
    uint64_t int_k;
    while (load_in >> op >> int_k) {
      k = int2key(int_k);
      assert(op == "INSERT");
      rolex_index->insert(k, randval(e));
      if (++ cnt % LOAD_HEARTBEAT == 0) {
        printf("thread %lu: %d load entries loaded.\n", loader_id, cnt);
      }
    }
  }
  else {  // string workloads
    std::string str_k;
    std::string line;
    while (std::getline(load_in, line)) {
      if (!line.size()) continue;
      std::istringstream tmp(line);
      tmp >> op >> str_k;
      k = str2key(str_k);
      assert(op == "INSERT");
      rolex_index->insert(k, randval(e));
      if (++ cnt % LOAD_HEARTBEAT == 0) {
        printf("thread %lu: %d load entries loaded.\n", loader_id, cnt);
      }
    }
  }
  printf("loader %lu load finish\n", loader_id);
}


void thread_run(int id) {
  bindCore(id * 2 + 1);  // bind to CPUs in NUMA that close to mlx5_2

  dsm->registerThread();
  uint64_t my_id = kThreadCount * dsm->getMyNodeID() + id;

  printf("I am %lu\n", my_id);

  if (id == 0) {
    bench_timer.begin();
  }

  // 1. insert ycsb_load
  if (id < std::min(kThreadCount, LOADER_NUM)) {
    thread_load(id);
  }

  // 2. load ycsb_trans
  Request* req = new Request[MAX_THREAD_REQUEST];
  int req_num = 0;
  std::ifstream trans_in(ycsb_trans_path + std::to_string(my_id));
  if (!trans_in.is_open()) {
    printf("Error opening trans file\n");
    assert(false);
  }
  std::string op;
  int cnt = 0;
  if (!kIsStr) {  // int workloads
    int range_size = 0;
    uint64_t int_k;
    while(trans_in >> op >> int_k) {
      if (op == "SCAN") trans_in >> range_size;
      else range_size = 0;
      Request r;
      r.req_type = (op == "READ"  ? SEARCH : (
                    op == "INSERT"? INSERT : (
                    op == "UPDATE"? UPDATE : SCAN
      )));
      r.range_size = fix_range_size >= 0 ? fix_range_size : range_size;
      r.k = int2key(int_k);
      req[req_num ++] = r;
      if (++ cnt % LOAD_HEARTBEAT == 0) {
        printf("thread %d: %d trans entries loaded.\n", id, cnt);
      }
    }
  }
  else {
    std::string str_k;
    std::string line;
    while (std::getline(trans_in, line)) {
      if (!line.size()) continue;
      std::istringstream tmp(line);
      tmp >> op >> str_k;
      Request r;
      r.req_type = (op == "READ"  ? SEARCH : (
                    op == "INSERT"? INSERT : (
                    op == "UPDATE"? UPDATE : SCAN
      )));
      assert(r.req_type != SCAN);  // string workloads currently does not support SCAN
      r.range_size = 0;
      r.k = str2key(str_k);
      req[req_num ++] = r;
      if (++ cnt % LOAD_HEARTBEAT == 0) {
        printf("thread %d: %d trans entries loaded.\n", id, cnt);
      }
    }
  }

  warmup_cnt.fetch_add(1);

  if (id == 0) {
    while (warmup_cnt.load() != kThreadCount)
      ;
    printf("node %d finish\n", dsm->getMyNodeID());
    dsm->barrier("warm_finish");

    uint64_t ns = bench_timer.end();
    printf("warmup time %lds\n", ns / 1000 / 1000 / 1000);

    ready = true;
    warmup_cnt.store(-1);
  }
  while (warmup_cnt.load() != -1)
    ;

  // 3. start ycsb test
  if (!kIsScan && kUseCoro) {
    rolex_index->run_coroutine(gen_func, work_func, kCoroCnt, req, req_num);
  }
  else {
    /// without coro
    rolex::Timer timer;
    auto gen = new RequsetGenBench(dsm, req, req_num, 0, 0);
    auto thread_id = dsm->getMyThreadID();

    while (!need_stop) {
      auto r = gen->next();

      timer.begin();
      work_func(rolex_index, r, nullptr);
      auto us_10 = timer.end() / 100;

      if (us_10 >= LATENCY_WINDOWS) {
        us_10 = LATENCY_WINDOWS - 1;
      }
      latency[thread_id][0][us_10]++;
    }
  }
}

void parse_args(int argc, char *argv[]) {
  if (argc != 6 && argc != 7) {
    printf("Usage: ./ycsb_test kNodeCount kThreadCount kCoroCnt workload_type[randint/email] workload_idx[a/b/c/d/e] [fix_range_size]\n");
    exit(-1);
  }

  kNodeCount = atoi(argv[1]);
  kThreadCount = atoi(argv[2]);
  kCoroCnt = atoi(argv[3]);
  kIsStr = (std::string(argv[4]) == "email");
  kIsScan = (std::string(argv[5]) == "e");
  kIsInsert = ((std::string(argv[5]) == "la") || (std::string(argv[5]) == "e"));

  std::string workload_dir;
  std::ifstream workloads_dir_in("../workloads.conf");
  if (!workloads_dir_in.is_open()) {
    printf("Error opening workloads.conf\n");
    assert(false);
  }
  workloads_dir_in >> workload_dir;
  ycsb_load_path = workload_dir + "/load_" + std::string(argv[4]) + "_workload" + std::string(argv[5]);
  ycsb_trans_path = workload_dir + "/txn_" + std::string(argv[4]) + "_workload" + std::string(argv[5]);
  if (argc == 7) {
    if(kIsScan) fix_range_size = atoi(argv[6]);
  }

  printf("kNodeCount %d, kThreadCount %d, kCoroCnt %d\n", kNodeCount, kThreadCount, kCoroCnt);
  printf("ycsb_load: %s\n", ycsb_load_path.c_str());
  printf("ycsb_trans: %s\n", ycsb_trans_path.c_str());
  if (argc == 7) {
    if(kIsScan) printf("fix_range_size: %d\n", fix_range_size);
  }
}

void save_latency(int epoch_id) {
  // sum up local latency cnt
  for (int i = 0; i < LATENCY_WINDOWS; ++i) {
    latency_th_all[i] = 0;
    for (int k = 0; k < MAX_APP_THREAD; ++k)
      for (int j = 0; j < MAX_CORO_NUM; ++j) {
        latency_th_all[i] += latency[k][j][i];
    }
  }
  // store in file
  std::ofstream f_out("../us_lat/epoch_" + std::to_string(epoch_id) + ".lat");
  f_out << std::setiosflags(std::ios::fixed) << std::setprecision(1);
  if (f_out.is_open()) {
    for (int i = 0; i < LATENCY_WINDOWS; ++i) {
      f_out << i / 10.0 << "\t" << latency_th_all[i] << std::endl;
    }
    f_out.close();
  }
  else {
    printf("Fail to write file!\n");
    assert(false);
  }
  memset(latency, 0, sizeof(uint64_t) * MAX_APP_THREAD * MAX_CORO_NUM * LATENCY_WINDOWS);
}

std::string build_key_str(uint64_t key) {
    if(rehash_key_) {
        key = util2::FNVHash64(key);
    }
    auto str = std::to_string(key);
    if(str.size() < define::keyLen) {
        return std::string(define::keyLen - str.size(), '0') + str;
    } else {
        return str.substr(0, define::keyLen);
    }
}

void load_train_keys(uint64_t perload_ops) {
    printf("Starting loading pre-train keys...\n");


    for(uint64_t i = 0; i < perload_ops; ++i) {
		auto key = build_key_str(i);
        Value value = 12;
        train_keys.emplace_back(str2key(key));
    }
    // 训练数据是放在计算侧执行的，还是内存侧执行的？
    // 如果是内存侧执行的，如何更新和同步计算侧的本地缓存
    printf("pre-train keys load finish\n");
}

int main(int argc, char *argv[]) {

  parse_args(argc, argv);

  DSMConfig config;
  assert(kNodeCount >= MEMORY_NODE_NUM);
  config.machineNR = kNodeCount;
  config.threadNR = kThreadCount;
  dsm = DSM::getInstance(config);
  bindCore(kThreadCount * 2 + 1);
  dsm->registerThread();

  uint64_t perload_ops = 100000000;
  load_train_keys(perload_ops);
  rolex_index = new RolexIndex(dsm, train_keys);

  // dsm->barrier("benchmark");
  dsm->set_barrier("init");
  dsm->set_barrier("finish");
  dsm->set_barrier("loading");
  dsm->set_barrier("running");

  while(true) {
    
  }
  for (int i = 0; i < kThreadCount; i ++) {
    th[i] = std::thread(thread_run, i);
  }

  while (!ready.load())
    ;
  timespec s, e;
  uint64_t pre_tp = 0;
  int count = 0;

  clock_gettime(CLOCK_REALTIME, &s);
  while(!need_stop) {

    sleep(TIME_INTERVAL);
    clock_gettime(CLOCK_REALTIME, &e);
    int microseconds = (e.tv_sec - s.tv_sec) * 1000000 +
                       (double)(e.tv_nsec - s.tv_nsec) / 1000;

    uint64_t all_tp = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      for (int j = 0; j < kCoroCnt; ++j)
        all_tp += tp[i][j];
    }
    clock_gettime(CLOCK_REALTIME, &s);

    uint64_t cap = all_tp - pre_tp;
    pre_tp = all_tp;

    uint64_t lock_fail_cnt = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      lock_fail_cnt += lock_fail[i];
    }

    uint64_t try_write_op_cnt = 0, write_handover_cnt = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      write_handover_cnt += write_handover_num[i];
      try_write_op_cnt += try_write_op[i];
    }

    uint64_t try_read_op_cnt = 0, read_handover_cnt = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      read_handover_cnt += read_handover_num[i];
      try_read_op_cnt += try_read_op[i];
    }

    uint64_t try_read_leaf_cnt = 0, read_leaf_retry_cnt = 0, leaf_read_syn_cnt = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      try_read_leaf_cnt += try_read_leaf[i];
      read_leaf_retry_cnt += read_leaf_retry[i];
      leaf_read_syn_cnt += leaf_read_syn[i];
    }

    double load_factor_sum_all = 0, split_hopscotch_cnt = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      load_factor_sum_all += load_factor_sum[i];
      split_hopscotch_cnt += split_hopscotch[i];
    }

    uint64_t correct_speculative_read_cnt = 0, try_speculative_read_cnt = 0, want_speculative_read_cnt = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      correct_speculative_read_cnt += correct_speculative_read[i];
      try_speculative_read_cnt += try_speculative_read[i];
      want_speculative_read_cnt += want_speculative_read[i];
    }

    std::map<uint64_t, uint64_t> range_cnt_sum;
    uint64_t range_cnt_sum_total = 0;
    for (int i = 0; i < MAX_APP_THREAD; ++i) {
      for (const auto& [range_size, cnt] : range_cnt[i]) {
        range_cnt_sum[range_size] += cnt;
        range_cnt_sum_total += cnt;
      }
    }

    std::fill(need_clear, need_clear + MAX_APP_THREAD, true);

#ifdef EPOCH_LAT_TEST
    save_latency(++ count);
#else
    if (++ count == TEST_EPOCH / 2) {  // rm latency during warm up
      memset(latency, 0, sizeof(uint64_t) * MAX_APP_THREAD * MAX_CORO_NUM * LATENCY_WINDOWS);
    }
#endif

    double per_node_tp = cap * 1.0 / microseconds;
    uint64_t cluster_tp = dsm->sum((uint64_t)(per_node_tp * 1000));  // only node 0 return the sum

    printf("%d, throughput %.4f\n", dsm->getMyNodeID(), per_node_tp);

    if (dsm->getMyNodeID() == 1) {
      for (const auto& [range_size, cnt] : range_cnt_sum) {
        printf("leaf_cnt=%lu ratio=%.2lf; ", range_size, (double)cnt / range_cnt_sum_total);
      }
      printf("\n\n");
    }

    if (dsm->getMyNodeID() == 0) {
      printf("epoch %d passed!\n", count);
      printf("cluster throughput %.3f Mops\n", cluster_tp / 1000.0);
      printf("avg. lock/cas fail cnt: %.4lf\n", lock_fail_cnt * 1.0 / try_write_op_cnt);
      printf("write combining rate: %.4lf\n", write_handover_cnt * 1.0 / try_write_op_cnt);
      printf("read delegation rate: %.4lf\n", read_handover_cnt * 1.0 / try_read_op_cnt);
      printf("read leaf retry rate: %.4lf\n", read_leaf_retry_cnt * 1.0 / try_read_leaf_cnt);
      printf("read sibling leaf rate: %.4lf\n", leaf_read_syn_cnt * 1.0 / try_read_leaf_cnt);
      printf("avg. leaf load factor: %.4lf\n", load_factor_sum_all * 1.0 / split_hopscotch_cnt);
      printf("speculative read rate: %.4lf\n", try_speculative_read_cnt * 1.0 / want_speculative_read_cnt);
      printf("correct ratio of speculative read: %.4lf\n", correct_speculative_read_cnt * 1.0 / try_speculative_read_cnt);
      printf("\n");
    }
    if (count >= TEST_EPOCH) {
      need_stop = true;
    }
  }
#ifndef EPOCH_LAT_TEST
  save_latency(1);
#endif
  for (int i = 0; i < kThreadCount; i++) {
    th[i].join();
    printf("Thread %d joined.\n", i);
  }
  rolex_index->statistics();
  printf("[END]\n");
  dsm->barrier("fin");

  return 0;
}
