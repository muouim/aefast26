#ifndef CLIENT_ADAPTIVE_H_
#define CLIENT_ADAPTIVE_H_

#include "DMCache.h"
#include "DMVerbs.h"
#include "InnerTree.h"
#include "LeafNode.h"
#include "LocalLockTable.h"
#include "RdmaKeeper.h"
#include "RdmaVerbs.h"
#include <unordered_map>

class DMTree {
private:
	static bool cmp(Entry a, Entry b) { return a.key < b.key; };

	Gaddr root_ptr_ptr; // the address which stores root pointer;
	static thread_local InnerTree inner_tree[define::kMaxCoro];
	static thread_local LeafNode leaf_nodes[define::kMaxCoro];
    static thread_local FPTable fp_tables[define::kMaxCoro];

	DMCache* dmcache;
	bool use_cache_mark;

	LocalLockTable* local_lock_table;
	// std::atomic<bool> is_sorted[kMaxBackSize];
	// LockFreeQueue<std::pair<Gaddr, uint64_t>> background_sort_pool;

private:
	void init_root_node();
	// read and write internal index
	Record find_leaf(const Key& key, int cord_id = 0,
	                 CoroContext* ctx = nullptr);
	Record read_bottom_inner(const Key& key, int coro_id, CoroContext* ctx);
    bool bottom_search(Gaddr addr, const Key& key, Record& record, bool &next_is_leaf, 
                          int coro_id, CoroContext* ctx);
	bool inner_search(Gaddr addr, const Key& key, Record& record,
	                  bool& next_is_leaf, int cord_id = 0,
	                  CoroContext* ctx = nullptr);
	void lock_node(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	               CoroContext* ctx = nullptr);
	void unlock_node(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	                 CoroContext* ctx = nullptr);
	bool inner_write(Gaddr &addr, Record& inner_record, Record& update_record, bool is_bottom, int coro_id = 0,
	                 CoroContext* ctx = nullptr);
	void inner_insert(const Key& key, Record& inner_record, Record& update_record, bool& from_cache, int coro_id = 0,
	                  CoroContext* ctx = nullptr);
	void node_split(Gaddr addr, Record& record, int coro_id = 0,
	                CoroContext* ctx = nullptr);

	// update root index and cache
	void get_root_addr(Gaddr& addr, int cord_id = 0,
	                   CoroContext* ctx = nullptr);
	void update_root(Record& record, Gaddr update_addr, int cord_id = 0,
	                 CoroContext* ctx = nullptr);

	// read leaf index
	void leaf_search(Gaddr addr, Record& path, bool& from_cache, const Key& key,
	                 Value& value, int coro_id = 0, CoroContext* ctx = nullptr);
	void leaf_insert(Gaddr& addr, Record& path, bool& from_cache,
	                 const Key& key, Value& value, int coro_id = 0,
	                 CoroContext* ctx = nullptr);
	LeafHeader get_leaf_hdr(Gaddr addr, int coro_id = 0,
	                        CoroContext* ctx = nullptr);

	void lock_entry(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	                CoroContext* ctx = nullptr);
	void unlock_entry(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	                  CoroContext* ctx = nullptr);
	bool try_lock_table(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	                    CoroContext* ctx = nullptr);
	void lock_table(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	                CoroContext* ctx = nullptr);
	void unlock_table(Gaddr addr, uint64_t lock_offset, int coro_id = 0,
	                  CoroContext* ctx = nullptr);
	bool leaf_resize(Gaddr addr, Gaddr fp_addr, const Key& key, Value& value,
	                 bool& from_cache, int coro_id = 0,
	                 CoroContext* ctx = nullptr);

	bool read_table(Gaddr addr, char* buffer, int entry_num, int coro_id = 0,
	                CoroContext* ctx = nullptr);
	void sort_table(Gaddr addr, Gaddr fp_addr, char* buffer, char* fp_buffer,
	                int cord_id = 0, CoroContext* ctx = nullptr);
	void leaf_scan(Gaddr addr, Record& path, bool& from_cache, const Key& from,
	               const Key& to, int len, EntrySortQueue& results,
	               int cord_id = 0, CoroContext* ctx = nullptr);
	void update_left_pointer(Gaddr pre_addr, int pre_capacity, Gaddr next_addr,
	                         int right_capacity, int cord_id = 0,
	                         CoroContext* ctx = nullptr);
	bool fast_search(const Key& key, Value& value, int cord_id = 0,
	                 CoroContext* ctx = nullptr);

public:
	DMVerbs* dmv;
	KeyCache key_cache_;

	bool read(const Key& key, Value& value, int cord_id = 0,
	          CoroContext* ctx = nullptr);
	void write(const Key& key, Value& value, int cord_id = 0,
	           CoroContext* ctx = nullptr);
	int scan(const Key& from_key, const Key& to_key, int len, int cord_id = 0,
	         CoroContext* ctx = nullptr);

	void background_sort_meta(Gaddr addr, int capacity, int cord_id = 0,
	                          CoroContext* ctx = nullptr);

	void print_cache_info() { dmcache->print_cache_info(); }

    void clear_cache() { dmcache = new DMCache(dmv->get_dsm_buffer()); }
	DMTree(DMVerbs* dmv)
	    : dmv(dmv) {

		use_cache_mark = ENABLE_CACHE;
		dmcache = new DMCache(dmv->get_dsm_buffer());
		local_lock_table = new LocalLockTable();
		init_root_node();
	};
	~DMTree() {};
};

#endif
