#ifndef INDEX_CACHE_H_
#define INDEX_CACHE_H_

#include "InnerTree.h"
#include "LeafNode.h"
#include "third_party/inlineskiplist.h"
#include "util/generic_cache.h"
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>

struct CacheEntry {
	Key key;
	InnerNode* node;
	Gaddr remote_addr;
};

class KeyCache {
public:
	void add(const Key& key, Gaddr page_addr, Gaddr entry_addr) {
		Entry value;
		value.page_addr = page_addr;
		value.entry_addr = entry_addr;
		cache_.add(key, value);
	}

	bool find(const Key& key, Gaddr& page_addr, Gaddr& entry_addr) {
		Entry value;
		if(cache_.find(key, value)) {
			page_addr = value.page_addr;
			entry_addr = value.entry_addr;
			return true;
		} else {
			return false;
		}
	}

	void erase(const Key& key) { cache_.erase(key); }

private:
	struct Entry {
		Gaddr page_addr;
		Gaddr entry_addr;
	};
	GenericCache<Key, Entry> cache_;
};

inline static CacheEntry Decode(const char* val) { return *(CacheEntry*)val; }

// cache comparator
struct CacheEntryComparator {
	typedef CacheEntry DecodedType;
	static DecodedType decode_key(const char* b) { return Decode(b); }

	int cmp(const DecodedType& a, const DecodedType& b) const {
		if(a.key > b.key) {
			return -1;
		}
		if(a.key < b.key) {
			return +1;
		}
		return 0;
	}

	int operator()(const char* a, const char* b) const {
		return cmp(Decode(a), Decode(b));
	}
	int operator()(const char* a, const DecodedType& b) const {
		return cmp(Decode(a), b);
	}
};
using CacheSkipList = InlineSkipList<CacheEntryComparator>;

class DMCache {
private:
	char* inner_cache_buffer;
	char* local_fp_buffer;

	// cache skiplist
	CacheEntryComparator cmp;
	Allocator alloc;
	CacheSkipList* skiplist;

	// cache pool
	int64_t all_node_num;
	std::atomic<int64_t> skipList_count;

	std::atomic<int64_t>  free_node_count;
	tbb::concurrent_queue<InnerNode*> cache_pool;
	tbb::concurrent_queue<InnerNode*> fifo_evict_pool;
	// LockFreeQueue<InnerNode*> cache_pool;
	// LockFreeQueue<InnerNode*> fifo_evict_pool;

	InnerNode* alloc_node_cache();
	InnerNode* evict_one();
	double cache_used();

public:
	DMCache(char* rdma_buffer) {
		local_fp_buffer = rdma_buffer;
		inner_cache_buffer = (char*)malloc(define::kInnerCacheSize * define::MB);
		// cache_buffer = (char*)malloc(define::kInnerCacheSize * define::MB);
		skiplist = new CacheSkipList(cmp, &alloc, 21);

		all_node_num = (define::kInnerCacheSize * define::MB) /
		               (InnerNodeSize + sizeof(CacheEntry));
		assert(all_node_num > 0);

		skipList_count.store(0);
		free_node_count.store(all_node_num - 1);

		for(uint64_t i = 0; i < all_node_num - 1; i++) {
			cache_pool.push(
			    (InnerNode*)(inner_cache_buffer + (uint64_t)(i * InnerNodeSize)));
		}
		std::cout << "Init Cache Size " << all_node_num << " Init Cache Size "
		          << all_node_num * InnerNodeSize / define::MB << std::endl;
	};
	~DMCache() {};

	void search_cache(const Key& key, Record& inner_record, std::vector<Record>& path);
	bool insert_cache(Record& record, InnerNode* node);
	void search_range_from_cache(const Key& from, const Key& to,
	                             std::vector<Record>& result);
	InnerNode* re_read_node(const Key& key);
	InnerNode* invalidate(const Key& key);

	void print_cache_info();
	void adapt_capacity(int ori_capacity, int& new_capacity);

	char* get_local_fp_buffer(const Gaddr& fp_addr) {
		return local_fp_buffer + fp_addr.offset;
	}
};

#endif
