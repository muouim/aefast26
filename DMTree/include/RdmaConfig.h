#ifndef _RDMA_CONFIG_H_
#define _RDMA_CONFIG_H_

#include "third_party/debug.h"
#include "third_party/wrlock.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cassert>
#include <city.h>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

#define MAX_THREAD_NUM 80
#define APP_MESSAGE_NR 96

#define NR_DIRECTORY 1
#define DIR_MESSAGE_NR 128

#define MAX_MACHINE_NUM 8

// auxiliary function
#define STRUCT_OFFSET(type, field) \
	(char*)&((type*)(0))->field - (char*)((type*)(0))

#define UNUSED(x) (void)(x)

#define ADD_ROUND(x, n) ((x) = ((x) + 1) % (n))

#define ROUND_UP(x, n) (((x) + (1 << (n)) - 1) & ~((1 << (n)) - 1))

#define ROUND_DOWN(x, n) ((x) & ~((1 << (n)) - 1))

#define MESSAGE_SIZE 96 // byte

#define RAW_RECV_CQ_COUNT 4096 // 128

namespace define {

constexpr uint64_t KB = 1024;
constexpr uint64_t MB = 1024ull * 1024;
constexpr uint64_t GB = 1024ull * MB;
constexpr uint16_t kCacheLineSize = 64;

// for remote allocate
constexpr uint64_t kChunkSize = 32 * MB;

// for store root pointer
constexpr uint64_t kRootPointerStoreOffest = kChunkSize / 2;
static_assert(kRootPointerStoreOffest % sizeof(uint64_t) == 0, "XX");

// lock on-chip memory
constexpr uint64_t kLockStartAddr = 0;
constexpr uint64_t kLockChipMemSize = 128 * 1024;

// number of locks
// we do not use 16-bit locks, since 64-bit locks can provide enough
// concurrency. if you want to use 16-bit locks, call *cas_dm_mask*
constexpr uint64_t kNumOfLock = kLockChipMemSize / sizeof(uint64_t);

// tune to an appropriate value (as small as possible without affect the
// performance)
constexpr uint64_t kLocalLockNum = 4 * MB;

// level of tree
constexpr uint64_t kMaxLevelOfTree = 27;

constexpr uint64_t rdmaBufferSize     = 8;         // GB  [CONFIG] 4
constexpr uint16_t kMaxCoro = 8;
constexpr int64_t  kPerThreadRdmaBuf  = rdmaBufferSize * GB / MAX_THREAD_NUM;
constexpr int64_t  kPerCoroRdmaBuf    = kPerThreadRdmaBuf / kMaxCoro;
// to large coro buffer, which mey affect the huge page cache/page fault

constexpr uint64_t kMemoryize = 1024 * 8; // MB
constexpr uint64_t kInnerCacheSize = kMemoryize / 2; // MB
constexpr uint64_t kLeafCacheSize = kMemoryize / 2; // MB
} // namespace define

#include <boost/coroutine/all.hpp>

using CoroYield = boost::coroutines::symmetric_coroutine<void>::yield_type;
using CoroCall = boost::coroutines::symmetric_coroutine<void>::call_type;

using CheckFunc = std::function<bool()>;
using CoroQueue = std::queue<std::pair<uint16_t, CheckFunc> >;
struct CoroContext {
	CoroYield* yield;
	CoroCall* master;
	CoroQueue* busy_waiting_queue;
	int coro_id;
};

inline void* hugePageAlloc(size_t size) {

	void* res = mmap(NULL, size, PROT_READ | PROT_WRITE,
	                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if(res == MAP_FAILED) {
		Debug::notifyError("mmap failed!\n");
	}

	return res;
}

inline void bindCore(uint16_t core) {
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);
	int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	if(rc != 0) {
		Debug::notifyError("can't bind core!");
	}
}

// global addresses
class Gaddr {
public:
	union {
		struct {
			uint64_t nodeID : 16;
			uint64_t offset : 48;
		};
		uint64_t val;
	};

	operator uint64_t() { return val; }

	static Gaddr Null() {
		static Gaddr zero{0, 0};
		return zero;
	};
	Gaddr(){};
	Gaddr(uint64_t n, uint64_t o) {
		nodeID = n;
		offset = o;
	}
} __attribute__((packed));

inline bool operator==(const Gaddr& lhs, const Gaddr& rhs) {
	return (lhs.nodeID == rhs.nodeID) && (lhs.offset == rhs.offset);
}

inline bool operator!=(const Gaddr& lhs, const Gaddr& rhs) {
	return !(lhs == rhs);
}

class CacheConfig {
public:
	uint32_t cacheSize;

    CacheConfig(uint32_t cacheSize = define::rdmaBufferSize)
	    : cacheSize(cacheSize) {}
};

class Cache {
public:
	Cache(const CacheConfig& cache_config) {
		size = cache_config.cacheSize;
		data = (uint64_t)hugePageAlloc(size * define::GB);
		// hugepage may affect small size access per coro due to hugepage fault
	}

	uint64_t data;
	uint64_t size;

private:
};

class DMConfig {
public:
	CacheConfig cacheConfig;
	uint32_t machineNR;
	uint64_t dsmSize; // G

	uint64_t ComputeNumber;
	uint64_t MemoryNumber;

	DMConfig(const CacheConfig& cacheConfig = CacheConfig(),
	         uint32_t machineNR = 2, uint64_t dsmSize = 8,
	         uint32_t ComputeNumber = 1, uint64_t MemoryNumber = 1)
	    : cacheConfig(cacheConfig)
	    , machineNR(machineNR)
	    , dsmSize(dsmSize)
	    , ComputeNumber(ComputeNumber)
	    , MemoryNumber(MemoryNumber) {}
};

// abstract rdma registered buffer
class RdmaBuffer {

private:
static const int kPageBufferCnt = 8;    // async, buffer safty
static const int kSiblingBufferCnt = 8; // async, buffer safty
static const int kCasBufferCnt = 64;     // async, buffer safty

	char* buffer;

	uint64_t* cas_buffer;
	uint64_t* unlock_buffer;
	uint64_t* zero_64bit;
	char* page_buffer;
	char* sibling_buffer;
	char* entry_buffer;

	int page_buffer_cur;
	int sibling_buffer_cur;
	int cas_buffer_cur;

	uint64_t kPageSize;

public:
	RdmaBuffer(char* buffer) {
		set_buffer(buffer);

		page_buffer_cur = 0;
		sibling_buffer_cur = 0;
		cas_buffer_cur = 0;
	}

	RdmaBuffer() = default;

	void set_buffer(char* buffer) {

		kPageSize = 2 * define::KB;
		this->buffer = buffer;
		cas_buffer = (uint64_t*)buffer;
		unlock_buffer =
		    (uint64_t*)((char*)cas_buffer + sizeof(uint64_t) * kCasBufferCnt);
		zero_64bit = (uint64_t*)((char*)unlock_buffer + sizeof(uint64_t));
		page_buffer = (char*)zero_64bit + sizeof(uint64_t);
		sibling_buffer = (char*)page_buffer + kPageSize * kPageBufferCnt;
		entry_buffer = (char*)sibling_buffer + kPageSize * kSiblingBufferCnt;
		*zero_64bit = 0;

		assert((char*)zero_64bit + 8 - buffer < define::kPerCoroRdmaBuf);
	}

	uint64_t* get_cas_buffer() {
		cas_buffer_cur = (cas_buffer_cur + 1) % kCasBufferCnt;
		return cas_buffer + cas_buffer_cur;
	}

	uint64_t* get_unlock_buffer() const { return unlock_buffer; }

	uint64_t* get_zero_64bit() const { return zero_64bit; }

	char* get_page_buffer() {
		page_buffer_cur = (page_buffer_cur + 1) % kPageBufferCnt;
		return page_buffer + (page_buffer_cur * kPageSize);
	}

	char* get_range_buffer() { return page_buffer; }

	char* get_sibling_buffer() {
		sibling_buffer_cur = (sibling_buffer_cur + 1) % kSiblingBufferCnt;
		return sibling_buffer + (sibling_buffer_cur * kPageSize);
	}

	char* get_entry_buffer() const { return entry_buffer; }
};

#endif