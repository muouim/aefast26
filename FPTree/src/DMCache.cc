#include "DMCache.h"

thread_local char* cache_entry_buf = nullptr;
thread_local int height = 0;
using CacheSkipList = InlineSkipList<CacheEntryComparator>;

// TODO: cache the entire tree, when caching misses, use the upper level interanl cache
// search cache
void DMCache::search_cache(const Key& key, Record& inner_record, std::vector<Record>& path) {

    // 查找时，是将remote_addr一块写到缓存中，如果缓存失效（缓存的叶子节点查找到了错误的节点，那么则进行远端节点的读取，再找不到，才进行完整索引的读取）
	CacheSkipList::Iterator iter(skiplist);
	CacheEntry e;

	e.key = key;
	iter.Seek((char*)&e);
	bool next_is_leaf = false;

	if(iter.Valid()) {
		auto val = (const CacheEntry*)iter.key();
		CacheEntry re = *(CacheEntry*)val;
		if(re.node == nullptr) {
			inner_record.addr = Gaddr::Null();
			return;
		}

		auto node = re.node;
		node->search(key, inner_record, next_is_leaf);

		if(inner_record.capacity == 0 || !node->check_hdr(key) ||
		   !node->check_consistent() || !next_is_leaf) {
			invalidate(key);
			inner_record.addr = Gaddr::Null();
		}
        Record upper;
        upper.key = node->get_hdr().min_key;
        upper.addr = re.remote_addr;
        path.clear();
        path.push_back(upper);
        path.push_back(inner_record);
	} else {
		inner_record.addr = Gaddr::Null();
	}
}

// insert to cache
bool DMCache::insert_cache(Record& record, InnerNode* node) {

	auto alloc_node = alloc_node_cache();
	if(alloc_node == nullptr) {
		return false;
	} else {
		memcpy((char*)alloc_node, (char*)node, InnerNodeSize);
	}

	auto buf =
	    skiplist->AllocateKey(sizeof(CacheEntry), height, &cache_entry_buf);
	auto& e = *(CacheEntry*)buf;
	e.key = record.key;
	e.node = alloc_node;
    e.remote_addr = record.addr;

	// update skiplist or insert skiplist
	if(skiplist->InsertConcurrently(buf) == false) {

		assert(cache_entry_buf != nullptr);
		CacheSkipList::Iterator iter(skiplist);
		CacheEntry u;

		u.key = record.key;
		iter.Seek((char*)&u);

		if(iter.Valid()) {
			auto val = (CacheEntry*)iter.key();
			if(val && u.key == val->key) {
				auto ptr = val->node;
				if(ptr == nullptr && __sync_bool_compare_and_swap(
				                         &(val->node), nullptr, alloc_node)) {
					fifo_evict_pool.push(alloc_node);
					return true;
				}
				free_node_count.fetch_add(1);
				cache_pool.push(alloc_node);
				return false;
			}
		}

		std::cout << "no find " << e.key.key << std::endl;
	} else {
		cache_entry_buf = nullptr;
		skipList_count++;
		fifo_evict_pool.push(alloc_node);
		return true;
	}
	return false;
}

// get cache buffer
InnerNode* DMCache::alloc_node_cache() {

	InnerNode* alloc_node = nullptr;

	uint64_t retry_count = 0;
	auto v = free_node_count.fetch_add(-1);
	if(v <= 0) {
		alloc_node = evict_one();
		while(alloc_node == nullptr) {
			alloc_node = evict_one();
			assert(retry_count++ < skipList_count);
		}
	} else {
        while(!cache_pool.try_pop(alloc_node)) {
            assert(retry_count++ < skipList_count);
        }
	}

	if(alloc_node == nullptr) {
		std::cout << "Cache Error " << std::endl;
		return nullptr;
	}

	return alloc_node;
}

// evict one frome cache
InnerNode* DMCache::evict_one() {

	uint64_t retry_count = 0;

	InnerNode* evict_node = nullptr;
	while(!(fifo_evict_pool.try_pop(evict_node) && evict_node)) {
		assert(retry_count++ < skipList_count);
	}

	auto node = (InnerNode*)evict_node;

	Key evict_key;
	evict_key = node->get_hdr().min_key;

	// TODO: remove the evicted cache entry
	CacheSkipList::Iterator iter(skiplist);
	CacheEntry e;

	e.key = evict_key;
	iter.Seek((char*)&e);

	if(iter.Valid()) {
		auto val = (CacheEntry*)iter.key();
		CacheEntry re = *(CacheEntry*)val;
		if(val->node == nullptr) {
			return nullptr;
		}
		if(__sync_bool_compare_and_swap(&(val->node), re.node, nullptr)) {
			free_node_count.fetch_add(1);
			return re.node;
		}
		return nullptr;
	}
	assert(0);
	return nullptr;
}

// invalidate one cache entry
InnerNode* DMCache::invalidate(const Key& key) {

	CacheSkipList::Iterator iter(skiplist);
	CacheEntry e;

	e.key = key;
	iter.Seek((char*)&e);

	if(iter.Valid()) {
		auto val = (CacheEntry*)iter.key();
		CacheEntry re = *(CacheEntry*)val;
		auto ptr = re.node;
		if(ptr == nullptr) {
			return nullptr;
		}
		if(__sync_bool_compare_and_swap(&(val->node), ptr, nullptr)) {
			free_node_count.fetch_add(1);
			cache_pool.push(re.node);
			return re.node;
		}
	}
	return nullptr;
}

// tunable leaf capacity
void DMCache::adapt_capacity(int ori_capacity, int& new_capacity) {

	// TODO: hash resizing scheme
	if(cache_used() > 0.99) {
		new_capacity = std::min(MaxNodeCapacity, ori_capacity * 2);
		if(ori_capacity * 2 > MaxNodeCapacity) {
			std::cout << "exceeded big table" << ori_capacity * 2 << std::endl;
		}
	} else {
		new_capacity = ori_capacity;
	}
}

// print cache information
void DMCache::print_cache_info() {
	// std::cout << "List size " << fifo_evict_pool.size() << " "
	//          << cache_pool.size() << std::endl;
	std::cout << "[skiplist node: " << skipList_count
	          << "] [cache used: " << (all_node_num - free_node_count.load())
	          << "] [size: "
	          << (all_node_num - free_node_count.load()) * InnerNodeSize << "]"
	          << std::endl;
}

// cache already used
double DMCache::cache_used() {
	return (double)((all_node_num - free_node_count.load()) /
	                (double)all_node_num);
}