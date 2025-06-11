#ifndef VERBS_H_
#define VERBS_H_

#include "DMConnect.h"
#include "Directory.h"
#include "RdmaConfig.h"
#include "RdmaKeeper.h"
#include "RdmaVerbs.h"
#include <algorithm>
#include <atomic>
#include <boost/coroutine/all.hpp>
#include <boost/crc.hpp>
#include <utility>

class LocalAllocator {
public:
	LocalAllocator() {
		head = Gaddr::Null();
		cur = Gaddr::Null();
	}

	Gaddr malloc(size_t size, bool& need_chunck, bool align = false) {
		if(align) {
		}

		Gaddr res = cur;
		if(log_heads.empty() ||
		   (cur.offset + size > head.offset + define::kChunkSize)) {
			need_chunck = true;
		} else {
			need_chunck = false;
			cur.offset += size;
		}
		// assert(res.addr + size <= 40 * define::GB);
		return res;
	}

	void set_chunck(Gaddr& addr) {
		log_heads.push_back(addr);
		head = cur = addr;
	}

	// TODO
	void free(const Gaddr& addr) {}

private:
	Gaddr head;
	Gaddr cur;
	std::vector<Gaddr> log_heads;
};

class DMVerbs {
public:
	DMVerbs(const DMConfig& conf);

	// static classe member function
	static DMVerbs* getInstance(const DMConfig& conf);

	// obtain netowrk resources for a thread
	void registerThread();
	void resetThread() { appID.store(0); }

	uint16_t getMyNodeID() { return myNodeID; }
	uint16_t getMyThreadID() { return thread_id; }
	uint16_t getClusterSize() { return conf.machineNR; }

	// RDMA operations
	void read(char* buffer, Gaddr gaddr, size_t size, bool signal = true,
	          CoroContext* ctx = nullptr);
	void read_sync(char* buffer, Gaddr gaddr, size_t size,
	               CoroContext* ctx = nullptr);

	void write(const char* buffer, Gaddr gaddr, size_t size, bool signal = true,
	           CoroContext* ctx = nullptr);
	void write_sync(const char* buffer, Gaddr gaddr, size_t size,
	                CoroContext* ctx = nullptr);
	void read_batch(RdmaOpRegion* rs, int k, bool signal = true,
	                CoroContext* ctx = nullptr);
	void read_batch_sync(RdmaOpRegion* rs, int k, CoroContext* ctx = nullptr);
	void write_batch(RdmaOpRegion* rs, int k, bool signal = true,
	                 CoroContext* ctx = nullptr);
	void write_batch_sync(RdmaOpRegion* rs, int k, CoroContext* ctx = nullptr);

	void write_faa(RdmaOpRegion& write_ror, RdmaOpRegion& faa_ror,
	               uint64_t add_val, bool signal = true,
	               CoroContext* ctx = nullptr);
	void write_faa_sync(RdmaOpRegion& write_ror, RdmaOpRegion& faa_ror,
	                    uint64_t add_val, CoroContext* ctx = nullptr);

	void write_cas(RdmaOpRegion& write_ror, RdmaOpRegion& cas_ror,
	               uint64_t equal, uint64_t val, bool signal = true,
	               CoroContext* ctx = nullptr);
	void write_cas_sync(RdmaOpRegion& write_ror, RdmaOpRegion& cas_ror,
	                    uint64_t equal, uint64_t val,
	                    CoroContext* ctx = nullptr);

	void cas(Gaddr gaddr, uint64_t equal, uint64_t val, uint64_t* rdma_buffer,
	         bool signal = true, CoroContext* ctx = nullptr);
	bool cas_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
	              uint64_t* rdma_buffer, CoroContext* ctx = nullptr);

	void cas_read(RdmaOpRegion& cas_ror, RdmaOpRegion& read_ror, uint64_t equal,
	              uint64_t val, bool signal = true, CoroContext* ctx = nullptr);
	bool cas_read_sync(RdmaOpRegion& cas_ror, RdmaOpRegion& read_ror,
	                   uint64_t equal, uint64_t val,
	                   CoroContext* ctx = nullptr);

	void cas_mask(Gaddr gaddr, uint64_t equal, uint64_t val,
	              uint64_t* rdma_buffer, uint64_t mask = ~(0ull),
	              bool signal = true, CoroContext* ctx = nullptr);
	bool cas_mask_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
	                   uint64_t* rdma_buffer, uint64_t mask = ~(0ull), CoroContext* ctx = nullptr);

	void faa_boundary(Gaddr gaddr, uint64_t add_val, uint64_t* rdma_buffer,
	                  uint64_t mask = 63, bool signal = true,
	                  CoroContext* ctx = nullptr);
	void faa_boundary_sync(Gaddr gaddr, uint64_t add_val, uint64_t* rdma_buffer,
	                       uint64_t mask = 63, CoroContext* ctx = nullptr);

	// for on-chip device memory
	void read_dm(char* buffer, Gaddr gaddr, size_t size, bool signal = true,
	             CoroContext* ctx = nullptr);
	void read_dm_sync(char* buffer, Gaddr gaddr, size_t size,
	                  CoroContext* ctx = nullptr);

	void write_dm(const char* buffer, Gaddr gaddr, size_t size,
	              bool signal = true, CoroContext* ctx = nullptr);
	void write_dm_sync(const char* buffer, Gaddr gaddr, size_t size,
	                   CoroContext* ctx = nullptr);

	void cas_dm(Gaddr gaddr, uint64_t equal, uint64_t val,
	            uint64_t* rdma_buffer, bool signal = true,
	            CoroContext* ctx = nullptr);
	bool cas_dm_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
	                 uint64_t* rdma_buffer, CoroContext* ctx = nullptr);

	void cas_dm_mask(Gaddr gaddr, uint64_t equal, uint64_t val,
	                 uint64_t* rdma_buffer, uint64_t mask = ~(0ull),
	                 bool signal = true);
	bool cas_dm_mask_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
	                      uint64_t* rdma_buffer, uint64_t mask = ~(0ull));

	void faa_dm_boundary(Gaddr gaddr, uint64_t add_val, uint64_t* rdma_buffer,
	                     uint64_t mask = 63, bool signal = true,
	                     CoroContext* ctx = nullptr);
	void faa_dm_boundary_sync(Gaddr gaddr, uint64_t add_val,
	                          uint64_t* rdma_buffer, uint64_t mask = 63,
	                          CoroContext* ctx = nullptr);

	uint64_t poll_rdma_cq(int count = 1);
	int poll_rdma_cqs(ibv_wc* wc);
	bool poll_rdma_cq_once(uint64_t& wr_id);

	uint64_t sum(uint64_t value) {
		static uint64_t count = 0;
		return keeper->sum(std::string("sum-") + std::to_string(count++),
		                   value);
	}

	// Memcached operations for sync
	size_t Put(uint64_t key, const void* value, size_t count) {

		std::string k = std::string("gam-") + std::to_string(key);
		keeper->memSet(k.c_str(), k.size(), (char*)value, count);
		return count;
	}

	size_t Get(uint64_t key, void* value) {

		std::string k = std::string("gam-") + std::to_string(key);
		size_t size;
		char* ret = keeper->memGet(k.c_str(), k.size(), &size);
		memcpy(value, ret, size);

		return size;
	}

private:
	void initRDMAConnection();
	void fill_keys_dest(RdmaOpRegion& ror, Gaddr addr, bool is_chip);

	DMConfig conf;
	std::atomic_int appID;
	Cache cache;

	// alloc resources and lock
	uint64_t alloc_memory_nodes;
	uint64_t alloc_compute_nodes;
	bool use_memory_fp;
	std::mutex alloc_lock;

	static thread_local int thread_id;
	static thread_local ThreadConnection* iCon;
	static thread_local char* rdma_buffer;

	static thread_local RdmaBuffer rbuf[define::kMaxCoro];
	static thread_local uint64_t thread_tag;

	uint64_t baseAddr;
	uint32_t myNodeID;
	LocalAllocator local_memory_allocator;
	LocalAllocator local_compute_allocator;

	ConnectionInfo* remoteInfo;
	ThreadConnection* thCon[MAX_THREAD_NUM];
	DirectoryConnection* dirCon[NR_DIRECTORY];
	RdmaKeeper* keeper;

	Directory* dirAgent[NR_DIRECTORY];

public:
	char* get_dsm_buffer() { return (char*)baseAddr; }
	bool is_register() { return thread_id != -1; }
	void barrier(const std::string& ss) { keeper->barrier(ss); }
	void set_barrier(const std::string& ss) { keeper->set_barrier(ss); }
	void barrier(const std::string& ss, int cs_num) {
		keeper->barrier(ss, cs_num);
	}

	char* get_rdma_buffer() { return rdma_buffer; }
	RdmaBuffer& get_rbuf(int coro_id) { return rbuf[coro_id]; }

	Gaddr memory_alloc(size_t size);
	Gaddr compute_alloc(size_t size);
	void free(Gaddr addr);

	void rpc_call_dir(const RawMessage& m, uint16_t node_id,
	                  uint16_t dir_id = 0) {

		auto buffer = (RawMessage*)iCon->message->getSendPool();

		memcpy(buffer, &m, sizeof(RawMessage));
		buffer->node_id = myNodeID;
		buffer->app_id = thread_id;

		iCon->sendMessage2Dir(buffer, node_id, dir_id);
	}

	RawMessage* rpc_wait() {
		ibv_wc wc;

		pollWithCQ(iCon->rpc_cq, 1, &wc);
		return (RawMessage*)iCon->message->getMessage();
	}
};

inline Gaddr DMVerbs::memory_alloc(size_t size) {
	alloc_lock.lock();

	thread_local int next_target_node = alloc_memory_nodes % conf.MemoryNumber;
	thread_local int next_target_dir_id =
	    (getMyThreadID() + getMyNodeID()) % NR_DIRECTORY;

	bool need_chunk = false;
	auto addr = local_memory_allocator.malloc(size, need_chunk);
	if(need_chunk) {
		RawMessage m;
		m.type = RpcType::MALLOC;

		this->rpc_call_dir(m, next_target_node, next_target_dir_id);
		local_memory_allocator.set_chunck(rpc_wait()->addr);

		if(++next_target_dir_id == NR_DIRECTORY) {
			// next_target_node = (next_target_node + 1) % conf.machineNR;
			alloc_memory_nodes = (alloc_memory_nodes + 1) % conf.MemoryNumber;
			next_target_node = alloc_memory_nodes;
			next_target_dir_id = 0;
		}

		// retry
		addr = local_memory_allocator.malloc(size, need_chunk);
	}
	alloc_lock.unlock();

	return addr;
}

inline Gaddr DMVerbs::compute_alloc(size_t size) {
	alloc_lock.lock();

    if(use_memory_fp) {
	    alloc_lock.unlock();
        return memory_alloc(size);
    }
	thread_local int next_target_node =
	    alloc_compute_nodes % conf.ComputeNumber;
	thread_local int next_target_dir_id =
	    (getMyThreadID() + getMyNodeID()) % NR_DIRECTORY;

	bool need_chunk = false;
	auto addr = local_compute_allocator.malloc(size, need_chunk);
	if(need_chunk) {
		RawMessage m;
		m.type = RpcType::MALLOC;

		// this->rpc_call_dir(m, next_target_node + conf.MemoryNumber, next_target_dir_id);
		this->rpc_call_dir(m, 0 + conf.MemoryNumber, next_target_dir_id);
		local_compute_allocator.set_chunck(rpc_wait()->addr);

		if(++next_target_dir_id == NR_DIRECTORY) {
			// next_target_node = (next_target_node + 1) % conf.machineNR;
			alloc_compute_nodes =
			    (alloc_compute_nodes + 1) % conf.ComputeNumber;
			next_target_node = alloc_compute_nodes;
			next_target_dir_id = 0;
		}

		// retry
		addr = local_compute_allocator.malloc(size, need_chunk);
	}
	alloc_lock.unlock();

    // TODO: consistent hash to decide which server as primary
    addr.nodeID = alloc_compute_nodes + conf.MemoryNumber;

    if(addr.offset >= (define::kLeafCacheSize - 1) * define::MB) {
        use_memory_fp = true;
        std::cout <<"need memory alloc " << " " << addr.offset << std::endl;
        return memory_alloc(size);
    }
	return addr;
}

inline void DMVerbs::free(Gaddr addr) {
	local_memory_allocator.free(addr);
	local_compute_allocator.free(addr);
}

#endif
