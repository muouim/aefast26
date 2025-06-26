#include "util/timer.h"
#include "util/ycsb.h"
#include <future>
#include <sys/types.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "TimberSaw/cache.h"
#include "db/table_cache.h"
#include "TimberSaw/comparator.h"
#include "TimberSaw/db.h"
#include "TimberSaw/env.h"
#include "TimberSaw/filter_policy.h"
#include "TimberSaw/write_batch.h"
#include "port/port.h"
#include "util/crc32c.h"
#include "util/histogram.h"
#include "util/mutexlock.h"
#include "util/random.h"
#include "util/testutil.h"

static int FLAGS_num = 1000000;

static int FLAGS_loads = -1;

static int FLAGS_load_vs_num = 1;

// Number of read operations to do.  If negative, do FLAGS_num reads.
static int FLAGS_reads = -1;

// Number of concurrent threads to run.
static int FLAGS_threads = 1;

// Size of each value
static int FLAGS_value_size = 8;
// Size of each value
static int FLAGS_key_size = 24;
// Arrange to generate values that shrink to this fraction of
// their original size after compression
static double FLAGS_compression_ratio = 0.5;

// Print histogram of operation timings
static bool FLAGS_histogram = false;

// print throughput for every 2 MIllion batch
static bool FLAGS_batchprint = false;

// Count the number of string comparisons performed
static bool FLAGS_comparisons = false;

// Number of bytes to buffer in memtable before compacting
// (initialized to default value by "main")
static int FLAGS_write_buffer_size = 0;

// Number of bytes written to each file.
// (initialized to default value by "main")
static int FLAGS_max_file_size = 0;

// Approximate size of user data packed per block (before compression.
// (initialized to default value by "main")
static int FLAGS_block_size = 0;

// Number of bytes to use as a table_cache of uncompressed data.
// Negative means use no table_cache.
static int FLAGS_cache_size = -1;

// Maximum number of files to keep open at the same time (use default if == 0)
static int FLAGS_open_files = 0;

static int FLAGS_block_restart_interval = 1;
// Bloom filter bits per key.
// Negative means use default settings.
static int FLAGS_bloom_bits = 10;

// Common key prefix length.
static int FLAGS_key_prefix = 0;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
static bool FLAGS_use_existing_db = false;

// if larger than 0, the compute node will always have this number of shards no matter how many memory
// nodes are there. THe shards will be distributed to the memory nodes in a round robin
// manner.
static int FLAGS_fixed_compute_shards_num = 0;
// whether the writer threads aware of the NUMA archetecture.
static bool FLAGS_enable_numa = false;
// If true, reuse existing log/MANIFEST files when re-opening a database.
static bool FLAGS_reuse_logs = false;

// Use the db with the following name.
static const char *FLAGS_db = nullptr;

static int FLAGS_readwritepercent = 90;
static int FLAGS_ops_between_duration_checks = 2000;
static int FLAGS_duration = 0;

using namespace TimberSaw;
TimberSaw::Env *g_env = nullptr;
TimberSaw::RDMA_Manager *rdma_mg = nullptr;
Cache *cache_;
const FilterPolicy *filter_policy_;

uint64_t number_of_key_total;
uint64_t number_of_key_per_compute;
uint64_t number_of_key_per_shard;

uint64_t perload_ops;
uint64_t tran_ops;

class CountComparator : public Comparator
{
public:
    CountComparator(const Comparator *wrapped) : wrapped_(wrapped) {}
    ~CountComparator() override {}
    int Compare(const Slice &a, const Slice &b) const override
    {
        count_.fetch_add(1, std::memory_order_relaxed);
        return wrapped_->Compare(a, b);
    }
    const char *Name() const override { return wrapped_->Name(); }
    void FindShortestSeparator(std::string *start,
                               const Slice &limit) const override
    {
        wrapped_->FindShortestSeparator(start, limit);
    }

    void FindShortSuccessor(std::string *key) const override
    {
        return wrapped_->FindShortSuccessor(key);
    }

    size_t comparisons() const { return count_.load(std::memory_order_relaxed); }

    void reset() { count_.store(0, std::memory_order_relaxed); }

private:
    mutable std::atomic<size_t> count_{0};
    const Comparator *const wrapped_;
};

CountComparator count_comparator_(BytewiseComparator());

#define COMPUTE_ID 0
#define MAX_MACHINE_NUM 8
using Value = uint64_t;

// #define USE_CORO
#define DMTREE_LATENCY
#define DMTREE_MAX_LATENCY_SIZE 100000

DB *db_;
WriteOptions write_options_;

uint64_t num_threads = 0;
int kCoroCnt = 4;
int kNodeCount = 7;
int kComputeNodeCount = 6;
int kMemoryNodeCount = 1;

using namespace std;
using namespace sds;
using OpRecord = util::OpRecord;

vector<uint64_t> all_keys;
bool rehash_key_ = true;
util::WorkloadBuilder *builder_[MAX_THREAD_NUM];

#ifdef DMTREE_LATENCY
uint64_t exeTime[MAX_THREAD_NUM][kMaxCoro];
struct timespec time_start[MAX_THREAD_NUM][kMaxCoro];
struct timespec time_end[MAX_THREAD_NUM][kMaxCoro];

double request_latency[MAX_THREAD_NUM][kMaxCoro];
uint64_t dis_latency[MAX_THREAD_NUM][kMaxCoro][DMTREE_MAX_LATENCY_SIZE];
#endif

thread_local uint64_t thread_finish_ops;
thread_local uint64_t thread_finish_coros;
atomic<uint64_t> finish_thread_count;

atomic<uint64_t> load_count;
uint64_t rdma_cnt[MAX_THREAD_NUM][MAX_MACHINE_NUM];
uint64_t rdma_bw[MAX_THREAD_NUM][MAX_MACHINE_NUM];


void saveKeysToFile(const std::string& filename) {
    for(uint64_t i = 0; i < perload_ops + tran_ops + 1024; i++) {
        uint64_t key = i;
        if (rehash_key_) {
            key = util::FNVHash64(key);
        }
        all_keys.push_back(key);
    }
    sort(all_keys.begin(), all_keys.end());
    std::ofstream outfile(filename, std::ios::binary);  
    if (!outfile) {  
        std::cerr << "can not write to file" << filename << std::endl;  
        return;  
    }  

    size_t size = all_keys.size();  
    outfile.write(reinterpret_cast<const char*>(&size), sizeof(size));  
    outfile.write(reinterpret_cast<const char*>(all_keys.data()), size * sizeof(uint64_t));  

    std::cout << "save to file: " << filename << std::endl;  
}  

bool loadKeysFromFile(const std::string& filename) {  
    std::ifstream infile(filename, std::ios::binary);  
    if (!infile) {  
        std::cerr << "can not read from file: " << filename << std::endl;  
        return false;  
    }  

    size_t size;  
    infile.read(reinterpret_cast<char*>(&size), sizeof(size));  

    all_keys.resize(size);  
    infile.read(reinterpret_cast<char*>(all_keys.data()), size * sizeof(uint64_t));  

    if (infile.gcount() != size * sizeof(uint64_t)) {  
        std::cerr << "read file error" << std::endl;  
        return false;  
    }  

    std::cout << "read from file: " << filename << std::endl;  
    return true;  
}  

void reset()
{
    cout << "A part finished." << endl;
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
        for (int j = 0; j < MAX_MACHINE_NUM; j++)
        {
            rdma_cnt[i][j] = 0;
            rdma_bw[i][j] = 0;
        }
    }
#ifdef DMTREE_LATENCY
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
        for (int j = 0; j < kMaxCoro; j++)
        {
            request_latency[i][j] = 0;
            memset(dis_latency[i][j], 0, sizeof(dis_latency[i][j]));
        }
    }
#endif
}

std::string build_key_str(uint64_t key)
{
    auto k = all_keys[key];
    auto str = std::to_string(k);
    if (str.size() < FLAGS_key_size)
    {
        return std::string(FLAGS_key_size - str.size(), '0') + str;
    }
    else
    {
        return str.substr(0, FLAGS_key_size);
    }
}

// Helper for quickly generating random data.
class RandomGenerator {
 private:
  std::string data_;
  int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < 1048576) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      // test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    pos_ = 0;
  }

  Slice Generate(size_t len) {
    if (pos_ + len > data_.size()) {
      pos_ = 0;
      assert(len < data_.size());
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }
};

Slice AllocateKey(std::unique_ptr<const char[]>* key_guard, size_t key_size) {
    char* data = new char[key_size];
    const char* const_data = data;
    key_guard->reset(const_data);
    return Slice(key_guard->get(), key_size);
}

void do_transaction(int th_id, Slice &key, RandomGenerator &a)
{
    auto &builder = builder_[th_id];

#ifdef DMTREE_LATENCY
    clock_gettime(CLOCK_REALTIME, &time_start[th_id][0]);
#endif
    OpRecord record;
    builder->fill_record(record);
    write_options_ = WriteOptions();
    ReadOptions options;

    uint64_t node_id = (TimberSaw::RDMA_Manager::node_id - 1) / 2;
    if (record.type != util::INSERT) {
        record.key = record.key % (perload_ops / rdma_mg->compute_nodes.size()) + node_id * number_of_key_per_compute;
    }

    Status s;
    auto k = build_key_str(record.key);
    key.Reset(k.c_str(), FLAGS_key_size);
    if(key.size() != FLAGS_key_size) {
        cout << "error " << key.ToString().c_str() << std::endl;
    }

    int rc;
    if (record.type == util::INSERT)
    {
        Value value = 12;
        s = db_->Put(write_options_, key, a.Generate(FLAGS_value_size));
        if (!s.ok())
        {
            cout << "Not insert " << key.ToString().c_str() << std::endl;
        }
    }
    if (record.type == util::READ)
    {
        string value;
        if (!db_->Get(options, key, &value).ok())
        {
            cout << "Not find " << key.ToString().c_str() << std::endl;
        }
    }
    if (record.type == util::UPDATE)
    {
        Value value = 12;
        s = db_->Put(write_options_, key, a.Generate(FLAGS_value_size));
    }
    if (record.type == util::SCAN)
    {
        std::vector<std::string> results;
        Iterator *iter = db_->NewIterator(options, key);
        iter->Seek(key);
        int count = 0;
        while (iter->Valid() && count < record.scan_len)
        {
            results.push_back(iter->value().data());
            iter->Next();
            count++;
        }
        if(iter->Valid())
            delete iter;
    }
    if (record.type == util::READMODIFYWRITE)
    {
        string value;
        if (db_->Get(options, key, &value).ok())
        {
        }
        s = db_->Put(write_options_, key, a.Generate(FLAGS_value_size));
    }

#ifdef DMTREE_LATENCY
    clock_gettime(CLOCK_REALTIME, &time_end[th_id][0]);
    exeTime[th_id][0] =
        (time_end[th_id][0].tv_sec - time_start[th_id][0].tv_sec) * 1000000000 +
        (time_end[th_id][0].tv_nsec - time_start[th_id][0].tv_nsec);
    request_latency[th_id][0] += exeTime[th_id][0];
    if (exeTime[th_id][0] / 100 >= DMTREE_MAX_LATENCY_SIZE)
    {
        cout << "long time " << exeTime[th_id][0] << " ns" << endl;
    }
    else
    {
        dis_latency[th_id][0][exeTime[th_id][0] / 100]++;
    }
#endif
}

uint64_t thread_load(uint64_t start, uint64_t num_ops)
{
    std::unique_ptr<const char[]> key_guard;
    Slice key = AllocateKey(&key_guard, FLAGS_key_size);
    RandomGenerator a;

    uint64_t count = 0;
    cout << "load range: [" << start << "," << start + num_ops << "]"
         << std::endl;
    Status s;

    for (uint64_t i = 0; i < num_ops; ++i)
    {
        if (load_count++ % 100000 == 0)
        {
            printf("load %d times\n", load_count.load());
        }
        auto k = build_key_str(start + i);
        Value value = 12;
        key.Reset(k.c_str(), FLAGS_key_size);
        if(key.size() != FLAGS_key_size) {
            cout << "error " << key.ToString().c_str() << std::endl;
        }
        s = db_->Put(write_options_, key, a.Generate(FLAGS_value_size));
        if (!s.ok())
        {
            cout << "Not insert " << key.ToString().c_str() << std::endl;
        }
        count++;
    }
    return count;
}

uint64_t thread_warm(uint64_t start, uint64_t num_ops)
{
    std::unique_ptr<const char[]> key_guard;
    Slice key = AllocateKey(&key_guard, FLAGS_key_size);
    RandomGenerator a;
    ReadOptions options;

    cout << "warm range: [" << start << "," << start + num_ops << "]"
         << std::endl;
    uint64_t count = 0;
    return count;

    for (uint64_t i = 0; i < num_ops; ++i)
    {
        auto k = build_key_str(start + i);
        key.Reset(k.c_str(), FLAGS_key_size);
        string value;
        if (!db_->Get(options, key, &value).ok())
        {
            cout << "Not find " << key.ToString().c_str() << std::endl;
        }
    }
    return count;
}

uint64_t thread_run(const uint64_t num_ops, const uint64_t tran_ops, int th_id)
{
    // dmv->registerThread();
    uint64_t count = 0;

#ifdef USE_CORO
    run_coroutine(num_ops, num_threads, kCoroCnt);
    count += num_ops;
#else
    std::unique_ptr<const char[]> key_guard;
    Slice key = AllocateKey(&key_guard, FLAGS_key_size);
    RandomGenerator a;

    for (uint64_t i = 0; i < num_ops; ++i)
    {
        do_transaction(th_id, key, a);
        count++;
    }
#endif
    assert(count == num_ops);
    return count;
}

void Open()
{
    assert(db_ == nullptr);
    Options options;
    options.env = g_env;
    options.create_if_missing = !FLAGS_use_existing_db;
    options.block_cache = cache_;
    options.write_buffer_size = FLAGS_write_buffer_size;
    options.max_file_size = FLAGS_max_file_size;
    options.block_size = FLAGS_block_size;
    options.bloom_bits = FLAGS_bloom_bits;
    options.block_restart_interval = FLAGS_block_restart_interval;
    if (FLAGS_comparisons)
    {
        options.comparator = &count_comparator_;
    }
#if TABLE_STRATEGY == 2
    //    options.max_open_files = FLAGS_open_files;

#else
    options.max_open_files = FLAGS_open_files;

#endif
    options.filter_policy = (FLAGS_bloom_bits >= 0
                           ? NewBloomFilterPolicy(FLAGS_bloom_bits)
                           : nullptr);
    options.reuse_logs = FLAGS_reuse_logs;
    //
    rdma_mg = Env::Default()->rdma_mg.get();
    // TODO: Keep every compute node have 100 million key range
    // number_of_key_total = FLAGS_num * FLAGS_threads * rdma_mg->compute_nodes.size(); // whole range.
    // number_of_key_per_compute = FLAGS_num * FLAGS_threads;
    number_of_key_total = perload_ops;
    number_of_key_per_compute = (perload_ops + tran_ops) / rdma_mg->compute_nodes.size();

    //    number_of_key_total = FLAGS_num*FLAGS_threads; // whole range.
    //    number_of_key_per_compute =
    //        number_of_key_total /rdma_mg->compute_nodes.size();
    // TODO: compute_shards
    if (FLAGS_fixed_compute_shards_num > 0)
    {
        options.StringShardInfo = new std::vector<std::pair<string, string>>();
        number_of_key_per_shard = number_of_key_per_compute / FLAGS_fixed_compute_shards_num;
        for (int i = 0; i < FLAGS_fixed_compute_shards_num; ++i)
        {
            uint64_t lower_bound = number_of_key_per_compute * (rdma_mg->node_id - 1) / 2 + i * number_of_key_per_shard;
            uint64_t upper_bound = number_of_key_per_compute * (rdma_mg->node_id - 1) / 2 + (i + 1) * number_of_key_per_shard;
            // in case that the number_of_key_per_shard is rounded down.
            if (i == FLAGS_fixed_compute_shards_num - 1)
            {
                upper_bound = number_of_key_per_compute * (rdma_mg->node_id + 1) / 2;
            }
            auto l = build_key_str(lower_bound);
            auto u = build_key_str(upper_bound);
            options.StringShardInfo->emplace_back(l, u);
        }
    }
    else if (rdma_mg->memory_nodes.size() > 1)
    {
        options.ShardInfo = new std::vector<std::pair<Slice, Slice>>();
        number_of_key_per_shard = number_of_key_per_compute / rdma_mg->memory_nodes.size();
        for (int i = 0; i < rdma_mg->memory_nodes.size(); ++i)
        {
            char *data_low = new char[FLAGS_key_size];
            char *data_up = new char[FLAGS_key_size];
            Slice key_low = Slice(data_low, FLAGS_key_size);
            Slice key_up = Slice(data_up, FLAGS_key_size);
            // TODO: 需要界定范围，所以无法采用rehash的key
            uint64_t lower_bound = number_of_key_per_compute * (rdma_mg->node_id - 1) / 2 + i * number_of_key_per_shard;
            uint64_t upper_bound = number_of_key_per_compute * (rdma_mg->node_id - 1) / 2 + (i + 1) * number_of_key_per_shard;
            if (i == rdma_mg->memory_nodes.size() - 1)
            {
                // in case that the number_of_key_pershard round down.
                upper_bound = number_of_key_per_compute * (rdma_mg->node_id + 1) / 2;
            }
            auto l = build_key_str(lower_bound);
            key_low = l;
            auto u = build_key_str(upper_bound);
            key_up = u;
            options.ShardInfo->emplace_back(key_low, key_up);
        }
    }
    for (auto iter : *options.StringShardInfo)
    {
        std::cout << "ashard range :" << iter.second << "~" << iter.first << std::endl;
    }
    Status s = DB::Open(options, FLAGS_db, &db_);
    if (!s.ok())
    {
        std::fprintf(stderr, "open error: %s\n", s.ToString().c_str());
        std::exit(1);
    }
}

extern std::atomic<int>not_th;
extern ibv_mr *t_mr[80];

int main(const int argc, const char *argv[])
{
    TimberSaw::RDMA_Manager::node_id = COMPUTE_ID + 1;
    load_count.store(0);
    finish_thread_count.store(0);
    perload_ops = 1000000000;
    tran_ops = 100000000;

    if(!loadKeysFromFile("entry.data")) {
        saveKeysToFile("entry.data");
    }

	num_threads = stoi(argv[1]);
    kCoroCnt = stoi(argv[2]);
    string worloads = argv[3];
    string distribution = argv[4];

    FLAGS_write_buffer_size = TimberSaw::Options().write_buffer_size;
    FLAGS_max_file_size = TimberSaw::Options().max_file_size;
    FLAGS_block_size = TimberSaw::Options().block_size;
    FLAGS_open_files = TimberSaw::Options().max_open_files;
    FLAGS_fixed_compute_shards_num = 8;

    g_env = TimberSaw::Env::Default();
    std::string default_db_path;

    // Choose a location for the test database if none given with --db=<path>
    if (FLAGS_db == nullptr)
    {
        g_env->GetTestDirectory(&default_db_path);
        default_db_path += "/dbbench";
        FLAGS_db = default_db_path.c_str();
    }
    Open();

    cout << "Loading" << rdma_mg->compute_nodes.size() << endl;
    cout << "creating benchmark" << endl;

    // per-load key-value entries
    vector<future<uint64_t>> actual_ops;

    int sum = 0;

    for (int i = 0; i < num_threads; ++i)
    {
        uint64_t node_id = (((rdma_mg->node_id - 1) / 2 )% rdma_mg->compute_nodes.size());
        uint64_t insert_start = node_id * number_of_key_per_compute + 
            (perload_ops / rdma_mg->compute_nodes.size());
        builder_[i] = util::WorkloadBuilder::Create(
            worloads.c_str(), distribution, perload_ops, insert_start, 0.99);
        assert(builder_[i]);
    }
    cout << "creating benchmark finish" << endl;
    rdma_mg->sync_with_computes_Cside();

    // per-load key-value entries
	num_threads = 1;
    uint64_t node_id = (TimberSaw::RDMA_Manager::node_id - 1) / 2;
    uint64_t start = node_id * number_of_key_per_compute;
    uint64_t thread_op = perload_ops / rdma_mg->compute_nodes.size() / num_threads;
    for (int i = 0; i < num_threads; ++i)
    {
        sleep(1);
        if (i == (num_threads - 1))
        {
            thread_op +=
                (perload_ops % (rdma_mg->compute_nodes.size() * num_threads));
            thread_op += rdma_mg->compute_nodes.size();
        }
        actual_ops.emplace_back(
            async(launch::async, thread_load,
                  start, thread_op));
        start += thread_op;
    }
    assert((int)actual_ops.size() == num_threads);

    for (auto &n : actual_ops)
    {
        assert(n.valid());
        sum += n.get();
    }
    cerr << "# Loading records:\t" << sum << endl;

    rdma_mg->sync_with_computes_Cside();

    reset();
    // clear cache
    // dmtree->clear_cache();
    // TODO: 更新缓存会导致很多Key被allocate？,为什么会超这么多？

    // read key-value entries to fill up the cache
	num_threads = 72;
    actual_ops.clear();
	start = node_id * number_of_key_per_compute;
    thread_op = perload_ops / rdma_mg->compute_nodes.size() / num_threads;
    for (int i = 0; i < num_threads; ++i)
    {
        if (i == (num_threads - 1))
        {
            thread_op +=
                (perload_ops % (rdma_mg->compute_nodes.size() * num_threads));
            thread_op += rdma_mg->compute_nodes.size();
        }
        actual_ops.emplace_back(
            async(launch::async, thread_warm, start, thread_op));
        start += thread_op;
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for (auto &n : actual_ops)
    {
        assert(n.valid());
        sum += n.get();
    }

    cerr << "# Warm records:\t" << sum << endl;
    reset();
    rdma_mg->sync_with_computes_Cside();

    for(int i = 0; i< 80; i++) {
        t_mr[i] = new ibv_mr{};
        Env::Default()->rdma_mg->Allocate_Local_RDMA_Slot(*t_mr[i], FlushBuffer);
    }

    // perform transactions
    num_threads = stoi(argv[1]);
    actual_ops.clear();
    utils::Timer<double> timer;
    timer.Start();
    for (int i = 0; i < num_threads; ++i)
    {
        actual_ops.emplace_back(
            async(launch::async, thread_run,
                  tran_ops / rdma_mg->compute_nodes.size() / num_threads, tran_ops, i));
    }
    assert((int)actual_ops.size() == num_threads);

    sum = 0;
    for (auto &n : actual_ops)
    {
        assert(n.valid());
        sum += n.get();
    }
    double duration = timer.End();

    // rdma_mg->sync_with_computes_Cside();
    // The waiting time here is not included in the performance statistics.
    // This ensures that no node exits prematurely, which could disrupt the subsequent steps of the script.
    sleep(30);
	cout << "----------------------------" << endl;
    cout << "Number of Thread: " << num_threads << endl;

#ifdef USE_CORO
    cout << "Number of OutStanding WR: " << kCoroCnt << endl;
#endif
    cout << "# Transaction throughput (MOPS): "
         << (tran_ops / rdma_mg->compute_nodes.size() / num_threads * num_threads) /
                duration / 1000 / 1000
         << endl;
    cout << "Total Time: " << duration << "s" << endl;
    cout << "Latency: "
         << duration /
                (tran_ops / rdma_mg->compute_nodes.size() / num_threads * num_threads) *
                1000 * 1000
         << " us" << endl;
    uint64_t rdma_count = 0;
    uint64_t rdma_bandwidth = 0;
    for (int i = 0; i < MAX_THREAD_NUM; ++i)
    {
        for (int j = 0; j < MAX_MACHINE_NUM; j++)
        {
            rdma_count += rdma_cnt[i][j];
            rdma_bandwidth += rdma_bw[i][j];
        }
    }
    printf("Total RDMA count: %ld\n", rdma_count);
    printf("Total RDMA IOPS (MOPS): %lf\n",
           rdma_count / duration / 1000 / 1000);
    printf("Total RDMA BW (Gbps): %lf\n",
           rdma_bandwidth * 8 / duration / 1024 / 1024 / 1024);

    for (int i = 0; i < MAX_MACHINE_NUM; i++)
    {
        uint64_t node_rdma = 0;
        uint64_t node_bw = 0;
        for (int j = 0; j < MAX_THREAD_NUM; ++j)
        {
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
    sleep(20);
#ifdef DMTREE_LATENCY
    int lat_count = 0;
    for (int i = 0; i < DMTREE_MAX_LATENCY_SIZE; i++)
    {
        for (int k = 0; k < kMaxCoro; k++)
        {
            for (int j = 0; j < MAX_THREAD_NUM; j++)
            {
                lat_count += dis_latency[j][k][i];
            }
        }
        if (lat_count > 0.50 * (double)((tran_ops / rdma_mg->compute_nodes.size() /
                                         num_threads * num_threads)))
        {
            cout << "# P50 " << i * 100 << " ns" << endl;
            break;
        }
    }
    lat_count = 0;
    for (int i = 0; i < DMTREE_MAX_LATENCY_SIZE; i++)
    {
        for (int k = 0; k < kMaxCoro; k++)
        {
            for (int j = 0; j < MAX_THREAD_NUM; j++)
            {
                lat_count += dis_latency[j][k][i];
            }
        }
        if (lat_count > 0.99 * (double)((tran_ops / rdma_mg->compute_nodes.size() /
                                         num_threads * num_threads)))
        {
            cout << "# P99 " << i * 100 << " ns" << endl;
            break;
        }
    }
#endif
    return 0;
}