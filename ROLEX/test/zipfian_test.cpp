#include "Rolex.h"
#include "Timer.h"
#include "zipf.h"

#include <city.h>
#include <stdlib.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>

// #define USE_CORO
#define TEST_EPOCH 10
// #define NO_WRITE_CONFLICT
// #define TEST_INSERT

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

int kReadRatio;
int kThreadCount;
int kNodeCount;


uint64_t kKeySpace = 60 * define::MB;
double kWarmRatio = 0.2;
double zipfan = 0.99;
int kCoroCnt = 2;
#ifdef TEST_INSERT
bool test_insert = true;  // micro-benchmark
#else
bool test_insert = false;
#endif

std::thread th[MAX_APP_THREAD];
uint64_t tp[MAX_APP_THREAD][MAX_CORO_NUM];

extern volatile bool need_stop;
extern volatile bool need_clear[MAX_APP_THREAD];
extern uint64_t latency[MAX_APP_THREAD][MAX_CORO_NUM][LATENCY_WINDOWS];
uint64_t latency_th_all[LATENCY_WINDOWS];

RolexIndex *rolex_index;
DSM *dsm;
std::vector<Key> train_keys;


inline Key to_key(uint64_t k) {
  return int2key(CityHash64((char *)&k, sizeof(k)) % kKeySpace);
}


class RequsetGenBench : public RequstGen {
public:
  RequsetGenBench(DSM *dsm, int coro_id, int coro_cnt):
                  id(dsm->getMyThreadID()), coro_id(coro_id), coro_cnt(coro_cnt) {
    seed = rdtsc();
    mehcached_zipf_init(&state, kKeySpace, zipfan,
                        (rdtsc() & (0x0000ffffffffffffull)) ^ id);
    all_thread = kThreadCount * dsm->getClusterSize();
    my_global_id = kThreadCount * dsm->getMyNodeID() + (id-1);
    insert_start_key = kWarmRatio * kKeySpace + kThreadCount * kCoroCnt * dsm->getMyNodeID() + id * kCoroCnt + coro_id;
  }

  Request next() override {
    Request r;
    r.req_type = (rand_r(&seed) % 100 < kReadRatio) ? SEARCH : INSERT;

#ifdef TEST_INSERT
    if (r.req_type == INSERT) {
      r.k = int2key(insert_start_key);
      insert_start_key += kThreadCount * kCoroCnt * dsm->getClusterSize();
    }
    else {
      int k = rand_r(&seed) % insert_start_key;
      if (!k) k = 1;
      r.k = int2key(k);
    }
#else
    uint64_t dis = mehcached_zipf_next(&state);
#ifdef NO_WRITE_CONFLICT
    if (r.req_type == INSERT) {
      auto tmp = CityHash64((char *)&dis, sizeof(dis)) % kKeySpace;
      tmp = tmp / all_thread * all_thread + my_global_id;
      while (tmp > kKeySpace) tmp -= all_thread;
      assert(tmp % all_thread == my_global_id);
      r.k = to_key(tmp);
    }
    else {
      r.k = to_key(dis);
    }
#else
    r.k = to_key(dis);
#endif
#endif
    r.v = ++ val;

    tp[id][coro_id]++;
    return r;
  }

private:
  int id;
  uint64_t all_thread;
  uint64_t my_global_id;
  int coro_id;
  int coro_cnt;
  uint64_t val = 0;
  unsigned int seed;
  struct zipf_gen_state state;
  uint64_t insert_start_key;
};


RequstGen *gen_func(DSM* dsm, Request* req, int req_num, int coro_id, int coro_cnt) {
  return new RequsetGenBench(dsm, coro_id, coro_cnt);
}


void work_func(RolexIndex *rolex, const Request& r, CoroPull* sink) {
  if (r.req_type == SEARCH) {
    Value v;
    rolex_index->search(r.k, v, sink);
  } else {
    rolex_index->insert(r.k, r.v, sink);
  }
}


rolex::Timer bench_timer;
std::atomic<int64_t> warmup_cnt{0};
std::atomic_bool ready{false};


void thread_load(int id) {
  // use 8 threads to load ycsb
  uint64_t all_loader_thread = std::min(kThreadCount, 8) * dsm->getClusterSize();
  uint64_t loader_id = std::min(kThreadCount, 8) * dsm->getMyNodeID() + id;
  printf("I am loader %lu\n", loader_id);

  uint64_t end_warm_key = kWarmRatio * kKeySpace;
  for (uint64_t i = 0; i < end_warm_key; ++i) {
    if (i % all_loader_thread == loader_id) {
      rolex_index->insert(to_key(i), i * 2);
    }
  }
  printf("loader %lu load finish\n", loader_id);
}

void thread_run(int id) {

  bindCore(id * 2 + 1);

  dsm->registerThread();

  uint64_t my_id = kThreadCount * dsm->getMyNodeID() + id;

  printf("I am %lu\n", my_id);

  if (id == 0) {
    bench_timer.begin();
  }

  if (id < std::min(kThreadCount, 8)) {
    thread_load(id);
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


#ifdef USE_CORO
  rolex_index->run_coroutine(gen_func, work_func, kCoroCnt);
#else
  /// without coro
  rolex::Timer timer;
  auto gen = new RequsetGenBench(dsm, 0, 0);
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
#endif
}

void parse_args(int argc, char *argv[]) {
  if (argc != 6) {
    printf("Usage: ./zipfian_test kNodeCount kReadRatio kThreadCount zipfan kCoroCnt\n");
    exit(-1);
  }

  kNodeCount = atoi(argv[1]);
  kReadRatio = atoi(argv[2]);
  kThreadCount = atoi(argv[3]);
  zipfan = atof(argv[4]);
  kCoroCnt = atoi(argv[5]);

  printf("kNodeCount %d, kReadRatio %d, kThreadCount %d, zipfan %lf, kCoroCnt %d\n", kNodeCount,
         kReadRatio, kThreadCount, zipfan, kCoroCnt);
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

void load_train_keys() {
  printf("Starting loading pre-train keys...\n");
  for (uint64_t i = define::kKeyMin; i < kKeySpace; ++ i) {
    train_keys.emplace_back(to_key(i));
  }
  printf("pre-train keys load finish\n");
}

int main(int argc, char *argv[]) {

  parse_args(argc, argv);

  DSMConfig config;
  assert(kNodeCount >= MEMORY_NODE_NUM);
  config.machineNR = kNodeCount;
  config.threadNR = kThreadCount;
  dsm = DSM::getInstance(config);
  dsm->registerThread();
  bindCore(kThreadCount * 2 + 1);

  load_train_keys();
  rolex_index = new RolexIndex(dsm, train_keys);

  dsm->barrier("benchmark");

  for (int i = 0; i < kThreadCount; i++) {
    th[i] = std::thread(thread_run, i);
  }

  while (!ready.load())
    ;

  timespec s, e;
  uint64_t pre_tp = 0;
  int count = 0;

  clock_gettime(CLOCK_REALTIME, &s);
  while(!need_stop) {
    sleep(0.5);
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

    save_latency(++ count);
    if (count >= TEST_EPOCH) {
      need_stop = true;
    }

    double per_node_tp = cap * 1.0 / microseconds;
    uint64_t cluster_tp = dsm->sum((uint64_t)(per_node_tp * 1000));

    printf("%d, throughput %.4f\n", dsm->getMyNodeID(), per_node_tp);

    if (dsm->getMyNodeID() == 1) {
      for (const auto& [range_size, cnt] : range_cnt_sum) {
        printf("leaf_cnt=%lu ratio=%.2lf; ", range_size, (double)cnt / range_cnt_sum_total);
      }
      printf("\n\n");
    }

    if (dsm->getMyNodeID() == 0) {
      printf("epoch %d passed!\n", count);
      printf("cluster throughput %.3f\n", cluster_tp / 1000.0);
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
  }
  printf("[END]\n");
  for (int i = 0; i < kThreadCount; i++) {
    th[i].join();
    printf("Thread %d joined.\n", i);
  }
  rolex_index->statistics();
  dsm->barrier("fin");

  return 0;
}
