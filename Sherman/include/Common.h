#ifndef __COMMON_H__
#define __COMMON_H__

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <atomic>
#include <bitset>
#include <limits>
#include <queue>

#include "Debug.h"
#include "HugePageAlloc.h"
#include "Rdma.h"

#include "WRLock.h"

#define KEY_SIZE 24
#define VALUE_SIZE 8
#define MEMORY_NR 1
// CONFIG_ENABLE_EMBEDDING_LOCK and CONFIG_ENABLE_CRC
// **cannot** be ON at the same time

// #define CONFIG_ENABLE_EMBEDDING_LOCK
// #define CONFIG_ENABLE_CRC

#define LATENCY_WINDOWS 1000000

#define STRUCT_OFFSET(type, field)                                             \
  (char *)&((type *)(0))->field - (char *)((type *)(0))

#define MAX_MACHINE 8

#define ADD_ROUND(x, n) ((x) = ((x) + 1) % (n))

#define MESSAGE_SIZE 96 // byte

#define POST_RECV_PER_RC_QP 128

#define RAW_RECV_CQ_COUNT 128

// { app thread
#define MAX_APP_THREAD 80

#define APP_MESSAGE_NR 96

// }

// { dir thread
#define NR_DIRECTORY 1

#define DIR_MESSAGE_NR 128
// }

void bindCore(uint16_t core);
char *getIP();
char *getMac();

inline int bits_in(std::uint64_t u) {
  auto bs = std::bitset<64>(u);
  return bs.count();
}

#include <boost/coroutine/all.hpp>

using CoroYield = boost::coroutines::symmetric_coroutine<void>::yield_type;
using CoroCall = boost::coroutines::symmetric_coroutine<void>::call_type;
using CoroQueue = std::queue<uint16_t>;

struct CoroContext {
  CoroYield *yield;
  CoroCall *master;
  CoroQueue *hot_wait_queue;
  int coro_id;
};

namespace define {

constexpr uint64_t MB = 1024ull * 1024;
constexpr uint64_t GB = 1024ull * MB;
constexpr uint16_t kCacheLineSize = 64;

// for remote allocate
constexpr uint64_t dsmSize = 8;        // GB  [CONFIG]
constexpr uint64_t kChunkSize = 32 * MB;

// for store root pointer
constexpr uint64_t kRootPointerStoreOffest = kChunkSize / 2;
static_assert(kRootPointerStoreOffest % sizeof(uint64_t) == 0, "XX");

// lock on-chip memory
constexpr uint64_t kLockStartAddr = 0;
constexpr uint64_t kLockChipMemSize = 128 * 1024;

// number of locks
// we do not use 16-bit locks, since 64-bit locks can provide enough concurrency.
// if you want to use 16-bit locks, call *cas_dm_mask*
constexpr uint64_t kNumOfLock = kLockChipMemSize / sizeof(uint64_t);

// level of tree
constexpr uint64_t kMaxLevelOfTree = 27;

constexpr uint16_t kMaxCoro = 8;
constexpr int64_t kPerCoroRdmaBuf = 128 * 1024;

constexpr uint8_t kMaxHandOverTime = 8;

constexpr int kIndexCacheSize = 6024; // MB
} // namespace define

static inline unsigned long long asm_rdtsc(void) {
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

// KeyType
class KeyType {
public:
	char key[KEY_SIZE];

	KeyType() { memset(key, 0, KEY_SIZE); }
	KeyType(const char* k) { memcpy(key, k, KEY_SIZE); }
	KeyType(const KeyType& k) { memcpy(key, k.key, KEY_SIZE); }
};

// For Tree
using Key = KeyType;
using Value = uint64_t;

__inline__ bool operator<(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) < 0);
}
__inline__ bool operator>(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) > 0);
}
__inline__ bool operator==(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) == 0);
}
__inline__ bool operator>=(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) >= 0);
}
__inline__ bool operator<=(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) <= 0);
}
__inline__ bool operator!=(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) != 0);
}
__inline__  KeyType operator-(KeyType a, const int &b) {
    a.key[KEY_SIZE - 1] = a.key[KEY_SIZE - 1] - b;
    return a;
}
__inline__  KeyType operator+(KeyType a, const int &b) {
    a.key[KEY_SIZE - 1] = a.key[KEY_SIZE - 1] + b;
    return a;
}
const Key kKeyMin;
const Key kKeyMax("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
const Key kNull;
const Value kValueNull = 0;

struct kv_cmp {
    bool operator()(const std::pair<KeyType, Value>& a, const std::pair<KeyType, Value>& b) {
        if(memcmp(a.first.key, b.first.key, KEY_SIZE) < 0) {
            return true;
        }
        return false;
    }
};

// Note: our RNICs can read 1KB data in increasing address order (but not for 4KB)
constexpr uint32_t kInternalPageSize = 1120;
constexpr uint32_t kLeafPageSize = 1166;
// constexpr uint32_t kInternalPageSize = 1120/16 + 90;
// constexpr uint32_t kLeafPageSize = 1166/16 + 95;
// constexpr uint32_t kInternalPageSize = 1120/8 + 80;
// constexpr uint32_t kLeafPageSize = 1166/8 + 85;
// constexpr uint32_t kInternalPageSize = 1120/4 + 75;
// constexpr uint32_t kLeafPageSize = 1166/4 + 80;
// constexpr uint32_t kInternalPageSize = 1120/2 + 50;
// constexpr uint32_t kLeafPageSize = 1166/2 + 60;
// constexpr uint32_t kInternalPageSize = 1120 * 1.91;
// constexpr uint32_t kLeafPageSize = 1166 * 1.95;
// constexpr uint32_t kInternalPageSize = 1120 * 3.74;
// constexpr uint32_t kLeafPageSize = 1166 * 3.82;
// constexpr uint32_t kInternalPageSize = 1120 * 7.41;
// constexpr uint32_t kLeafPageSize = 1166 * 7.56;
// constexpr uint32_t kInternalPageSize = 1120 * 14.72;
// constexpr uint32_t kLeafPageSize = 1166 * 15.02;

__inline__ unsigned long long rdtsc(void) {
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

inline void mfence() { asm volatile("mfence" ::: "memory"); }

inline void compiler_barrier() { asm volatile("" ::: "memory"); }

#endif /* __COMMON_H__ */
