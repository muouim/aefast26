#include "DMTree.h"

thread_local InnerTree DMTree::inner_tree[define::kMaxCoro];
thread_local LeafNode DMTree::leaf_nodes[define::kMaxCoro];
thread_local FPTable DMTree::fp_tables[define::kMaxCoro];

void DMTree::leaf_search(Gaddr addr, Record& path, bool& from_cache,
                         const Key& key, Value& value, int coro_id,
                         CoroContext* ctx) {

	auto& leaf_node = leaf_nodes[coro_id];
	leaf_node.attach_node(path.capacity);

	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();
	
    auto fp_buffer = buffer + sizeof(Entry);
    auto fp_table = &fp_tables[coro_id];
    fp_table->attach(fp_buffer, path.capacity);

    bool is_sync_leaf = false;
leaf_search_reread:
	// read the fp locally
	fp_table->attach(fp_buffer, path.capacity);
	dmv->read_sync(fp_buffer, path.fp_addr, fp_table->get_size(), ctx);

	if(!fp_table->check_consistent()) {
		goto leaf_search_reread;
	}

	if(fp_table->get_version() != path.version) {
		if(from_cache && !is_sync_leaf) {
        	dmcache->invalidate(key);
            is_sync_leaf = true;
            path = read_bottom_inner(key, coro_id, ctx);
		} else {
            from_cache = false;
            path = find_leaf(key, coro_id, ctx);
        }
        addr = path.addr;
        leaf_node.attach_node(path.capacity);
		goto leaf_search_reread;
	}

	// locate entry, read entry from the memory side
	auto fp = leaf_node.cal_key_fp(key);

	for(int i = 0; i < path.capacity; i++) {
		if(fp_table->fp[i] != fp) {
			continue;
		}

		auto e_offset = leaf_node.get_entry_offset(i);
		Gaddr entry_addr(addr.nodeID, addr.offset + e_offset);

		Entry* entry = nullptr;
		do {
			dmv->read_sync(buffer, entry_addr, sizeof(Entry), ctx);
			entry = (Entry*)buffer;
		} while(!entry->check_consistent());

		if(entry->get_version() != path.version) {
            if(from_cache && !is_sync_leaf) {
        	    dmcache->invalidate(key);
                is_sync_leaf = true;
                path = read_bottom_inner(key, coro_id, ctx);
            } else {
                from_cache = false;
                path = find_leaf(key, coro_id, ctx);
            }
            addr = path.addr;
            leaf_node.attach_node(path.capacity);
			goto leaf_search_reread;
		}
		if(entry->search(key, value)) {
			return;
		}
	}

	std::cout << "no key: " << key << std::endl;
}

// lock entry
void DMTree::lock_entry(Gaddr addr, uint64_t lock_offset, int coro_id,
                        CoroContext* ctx) {

	auto cas_buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();

	Gaddr lock_addr(addr.nodeID, addr.offset + lock_offset);

	uint64_t retry_cnt = 0;
	while(!dmv->cas_sync(lock_addr, 0ull, 1ull, cas_buffer, ctx)) {
		retry_cnt++;

		if(retry_cnt > 1000000) {
			std::cout << "Deadlock " << lock_addr << std::endl;
			std::cout << dmv->getMyNodeID() << ", " << dmv->getMyThreadID()
			          << std::endl;
			assert(false);
		}
	}
}

// unlock entry
void DMTree::unlock_entry(Gaddr addr, uint64_t lock_offset, int coro_id,
                          CoroContext* ctx) {

	auto buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();

	Gaddr lock_addr(addr.nodeID, addr.offset + lock_offset);

	*buffer = 0;
	dmv->write_sync((char*)buffer, lock_addr, sizeof(uint64_t), ctx);
}

// get header of the table
LeafHeader DMTree::get_leaf_hdr(Gaddr addr, int coro_id, CoroContext* ctx) {

	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();
	Gaddr hdr_addr(addr.nodeID, addr.offset);

	dmv->read_sync(buffer, hdr_addr, sizeof(LeafHeader), ctx);

	return *(LeafHeader*)buffer;
}

// lock entry
void DMTree::lock_table(Gaddr addr, uint64_t lock_offset, int coro_id,
                        CoroContext* ctx) {

	auto cas_buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();

	Gaddr lock_addr(addr.nodeID, addr.offset + lock_offset);

	uint64_t retry_cnt = 0;
	while(!dmv->cas_sync(lock_addr, 0ull, 1ull, cas_buffer, ctx)) {
		retry_cnt++;

		if(retry_cnt > 1000000) {
			std::cout << "Deadlock " << lock_addr << std::endl;
			std::cout << dmv->getMyNodeID() << ", " << dmv->getMyThreadID()
			          << std::endl;
			assert(false);
		}
	}
}

// inert key-value entry into leaf hash table
void DMTree::leaf_insert(Gaddr& addr, Record& path, bool& from_cache,
                         const Key& key, Value& value, int coro_id,
                         CoroContext* ctx) {

	auto& leaf_node = leaf_nodes[coro_id];
	leaf_node.attach_node(path.capacity);

	// need to be reference instead of variable
	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();
	
    auto fp_buffer = buffer + sizeof(Entry);
    auto fp_table = &fp_tables[coro_id];
    fp_table->attach(fp_buffer, path.capacity);

    bool is_sync_leaf = false;
leaf_insert_reread:
	// read the fp from the compute side
	fp_table->attach(fp_buffer, path.capacity);
	auto fp_lock_offset = fp_table->get_lock_offset();

	lock_table(path.fp_addr, fp_lock_offset, coro_id, ctx);

	dmv->read_sync(fp_buffer, path.fp_addr, fp_table->get_size(), ctx);
	assert(fp_table->check_consistent());

	if(fp_table->get_version() != path.version) {
		unlock_table(path.fp_addr, fp_lock_offset, coro_id, ctx);

		if(from_cache && !is_sync_leaf) {
            is_sync_leaf = true;
        	dmcache->invalidate(key);
            path = read_bottom_inner(key, coro_id, ctx);
		} else {
            from_cache = false;
            path = find_leaf(key, coro_id, ctx);
        }
        addr = path.addr;
        leaf_node.attach_node(path.capacity);

		goto leaf_insert_reread;
	}

	// search the entry from the memory side
	auto fp = leaf_node.cal_key_fp(key);

	for(int i = 0; i < path.capacity; i++) {

		if(fp_table->fp[i] != fp) {
			continue;
		}

		auto e_offset = leaf_node.get_entry_offset(i);
		Gaddr entry_addr(addr.nodeID, addr.offset + e_offset);

		dmv->read_sync(buffer, entry_addr, sizeof(Entry), ctx);
		auto entry = (Entry*)buffer;

		assert(entry->check_consistent());
		assert(entry->get_version() == path.version);

#ifdef ENABLE_WRITE_COMBINING
	    local_lock_table->get_combining_value(key, value);
#endif
		if(entry->insert(key, value)) {
			dmv->write_sync(buffer, entry_addr, sizeof(Entry), ctx);
			unlock_table(path.fp_addr, fp_lock_offset, coro_id, ctx);
			return;
		}
	}

	// insert new fp to the fp table
	int e_id = -1;
	if(fp_table->insert(fp, e_id)) {
		auto e_offset = leaf_node.get_entry_offset(e_id);
		Gaddr entry_addr(addr.nodeID, addr.offset + e_offset);

		auto entry = (Entry*)buffer;
		memset(entry, 0, sizeof(Entry));
		entry->set_version(path.version);
#ifdef ENABLE_WRITE_COMBINING
	    local_lock_table->get_combining_value(key, value);
#endif
		assert(entry->insert(key, value));
		dmv->write_sync(buffer, entry_addr, sizeof(Entry), ctx);

		fp_table->unlock();
		dmv->write_sync(fp_buffer, path.fp_addr, fp_table->get_size(), ctx);
		return;
	}
	leaf_resize(addr, path.fp_addr, key, value, from_cache, coro_id, ctx);
}

bool DMTree::try_lock_table(Gaddr addr, uint64_t lock_offset, int coro_id,
                            CoroContext* ctx) {

	auto cas_buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();

	Gaddr lock_addr(addr.nodeID, addr.offset + lock_offset);

	if(dmv->cas_sync(lock_addr, 0ull, 1ull, cas_buffer, ctx)) {
		return true;
	}
	return false;
}

void DMTree::unlock_table(Gaddr addr, uint64_t lock_offset, int coro_id,
                          CoroContext* ctx) {

	auto cas_buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();

	Gaddr lock_addr(addr.nodeID, addr.offset + lock_offset);

	*cas_buffer = 0;
	dmv->write_sync((char*)cas_buffer, lock_addr, sizeof(uint64_t), ctx);
}

// leaf hash table split
bool DMTree::leaf_resize(Gaddr addr, Gaddr fp_addr, const Key& key,
                         Value& value, bool& from_cache, int coro_id, CoroContext* ctx) {

	auto& leaf_node = leaf_nodes[coro_id];
	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();
	auto cur_fp_buffer = buffer + leaf_node.cur_node_size();

	FPTable* cur_fp_table =
	    new FPTable(cur_fp_buffer, leaf_node.cur_capacity());
	memset((char*)cur_fp_buffer, 0, cur_fp_table->get_size());

#ifdef ENABLE_WRITE_COMBINING
	local_lock_table->get_combining_value(key, value);
#endif

	EntryPos insert_entry(key, value);

	std::vector<EntryPos> node_entries;
	node_entries.push_back(insert_entry);

	dmv->read_sync(buffer, addr, leaf_node.cur_node_size(), ctx);

	for(int i = 0; i < leaf_node.cur_capacity(); i++) {
		auto e_offset = leaf_node.get_entry_offset(i);
		auto entry = (Entry*)(buffer + e_offset);

		assert(entry->check_consistent());
		entry->add_version();

		if(entry->valid == 1) {
			EntryPos entry_pos = *entry;
			node_entries.push_back(entry_pos);
			entry->clean();
		}
	}

	int current_capacity = leaf_node.cur_capacity();
	int sibling_capacity = leaf_node.cur_capacity();

#ifdef USE_HASH_RESIZING
	dmcache->adapt_capacity(current_capacity, sibling_capacity);
#endif

	// find half kv to split table
	int split = node_entries.size() / 2;
	if(sibling_capacity > current_capacity) {
		split = 0;
	}
	sort(node_entries.begin(), node_entries.end(), entry_cmp());

	// allocate new leaf node
	leaf_node.attach_node(sibling_capacity);
	Gaddr sibling_addr = dmv->memory_alloc(leaf_node.cur_node_size());

	auto sibling_buffer = (dmv->get_rbuf(coro_id)).get_sibling_buffer();
	memset(sibling_buffer, 0, leaf_node.cur_node_size());

	for(int i = 0; i < sibling_capacity; i++) {
		auto e_offset = leaf_node.get_entry_offset(i);
		auto entry = (Entry*)(sibling_buffer + e_offset);
		entry->set_consistent();
	}

	// allocate new leaf fp table
	auto sib_fp_buffer = sibling_buffer + leaf_node.cur_node_size();

	FPTable* sib_fp_table =
	    new FPTable(sib_fp_buffer, leaf_node.cur_capacity());
	memset((char*)sib_fp_buffer, 0, sib_fp_table->get_size());

	Gaddr sibling_fp_addr = dmv->memory_alloc(sib_fp_table->get_size());

	// update the leaf header
	LeafHeader sibling_hdr = *(LeafHeader*)buffer;
	LeafHeader current_hdr = *(LeafHeader*)buffer;

	current_hdr.version++;
	current_hdr.update_right(sibling_addr, sibling_fp_addr, sibling_capacity);
	current_hdr.max_key = node_entries[split].key;
	cur_fp_table->set_version(current_hdr.version);

	// split the leaf table
	for(int i = 0; i < node_entries.size(); i++) {
		LeafHeader* hdr = nullptr;
		char* insert_buffer = nullptr;
		FPTable* fp_table = nullptr;

		if(i < split) {
			fp_table = cur_fp_table;
			hdr = &current_hdr;
			insert_buffer = buffer;
			leaf_node.attach_node(current_capacity);
		} else {
			fp_table = sib_fp_table;
			hdr = &sibling_hdr;
			insert_buffer = sibling_buffer;
			leaf_node.attach_node(sibling_capacity);
		}

		auto fp = leaf_node.cal_key_fp(node_entries[i].key);

		int e_id = -1;
		assert(fp_table->insert(fp, e_id));
		auto e_offset = leaf_node.get_entry_offset(e_id);
		auto entry = (Entry*)(insert_buffer + e_offset);
		assert(entry->insert(node_entries[i].key, node_entries[i].value));
	}

	sibling_hdr.version = 0;
	sibling_hdr.capacity = sibling_capacity;
	sibling_hdr.min_key = current_hdr.max_key;

	*(LeafHeader*)buffer = current_hdr;
	*(LeafHeader*)sibling_buffer = sibling_hdr;

	// write table
	leaf_node.attach_node(sibling_capacity);
	dmv->write_sync(sibling_buffer, sibling_addr, leaf_node.cur_node_size(),
	                ctx);
	dmv->write_sync(sib_fp_buffer, sibling_fp_addr, sib_fp_table->get_size(),
	                ctx);

	leaf_node.attach_node(current_capacity);
	dmv->write_sync(buffer, addr, leaf_node.cur_node_size(), ctx);
	dmv->write_sync(cur_fp_buffer, fp_addr, cur_fp_table->get_size(), ctx);
	delete cur_fp_table;
	delete sib_fp_table;

	// index record insert to upper level
	Record inner_record(sibling_hdr.min_key, sibling_addr, sibling_fp_addr,
	                    sibling_capacity, sibling_hdr.version);

	Record update_record(current_hdr.min_key, addr, fp_addr, current_capacity,
	                     current_hdr.version);
	inner_insert(key, inner_record, update_record, from_cache, coro_id, ctx);

	return true;
}

bool DMTree::read_table(Gaddr addr, char* buffer, int entry_num, int coro_id,
                        CoroContext* ctx) {

	auto& leaf_node = leaf_nodes[coro_id];

	dmv->read_sync(buffer, addr, leaf_node.cur_node_size(), ctx);

	uint64_t version = 0;
	for(int i = 0; i < leaf_node.cur_capacity(); i++) {
		auto e_offset = leaf_node.get_entry_offset(i);
		auto entry = (Entry*)(buffer + e_offset);

		/*
		if(i == entry_num) {
		    assert(entry->valid == 0);
		}
		*/
		if(!entry->check_consistent()) {
			return false;
		}
		if(i == 0) {
			version = entry->get_version();
		} else {
			if(version != entry->get_version()) {
				return false;
			}
		}
	}

	return true;
}

// scan leaf hash table
void DMTree::leaf_scan(Gaddr addr, Record& path, bool& from_cache,
                       const Key& from, const Key& to, int len,
                       EntrySortQueue& results, int coro_id, CoroContext* ctx) {

	auto& leaf_node = leaf_nodes[coro_id];
	leaf_node.attach_node(path.capacity);

	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();

    bool is_sync_leaf = false;
	bool head_node = true;
	while(addr != Gaddr::Null() && len > 0) {
		auto entry_num = leaf_node.cur_capacity();
		while(!read_table(addr, buffer, entry_num, coro_id, ctx)) {
		}

		auto hdr = (LeafHeader*)buffer;
		if(head_node == true && hdr->version != path.version) {
            if(from_cache && !is_sync_leaf) {
                is_sync_leaf = true;
                path = read_bottom_inner(from, coro_id, ctx);
            } else {
                from_cache = false;
                path = find_leaf(from, coro_id, ctx);
            }
            addr = path.addr;
            leaf_node.attach_node(path.capacity);
			continue;
		}
		head_node = false;
		assert(path.capacity == hdr->capacity);

		if(to < hdr->min_key) {
			break;
		}

		// decide whether the table has been modified
		for(int i = 0; i < entry_num; i++) {
			auto e_offset = leaf_node.get_entry_offset(i);
			auto entry = (Entry*)(buffer + e_offset);

			assert(entry->check_consistent());

			if(entry->valid == 1 && entry->key >= from) {
				EntryPos entry_pos = *entry;
				results.push(entry_pos);
				len--;
			}
		}
		/*
		std::cout << "scan addr: " << addr.offset
		          << ", capacity: " << path.capacity << ", start key: " << from
		<< ", scan len: " << len
		          << ", current node max: " << hdr->max_key
		          << ", min: " << hdr->min_key << std::endl;
		*/
		addr = hdr->right_addr;
		if(addr == Gaddr::Null()) {
			break;
		}
		path.capacity = hdr->right_capacity;
		path.fp_addr = hdr->right_fp_addr;
		leaf_node.attach_node(path.capacity);
	}
	// std::cout<< "--------------------------------"<<std::endl;
}

// public functions - read
bool DMTree::read(const Key& key, Value& value, int coro_id, CoroContext* ctx) {
	Gaddr leaf_addr = Gaddr::Null();
	Record inner_record;

	bool search_res = false;
	std::pair<bool, bool> lock_res = std::make_pair(false, false);
	bool read_handover = false;

#ifdef ENABLE_READ_DELEGATION
	lock_res = local_lock_table->acquire_local_read_lock(key, ctx, coro_id);
	read_handover = (lock_res.first && !lock_res.second);
#endif

	if(read_handover) {
#ifdef ENABLE_READ_DELEGATION
		local_lock_table->release_local_read_lock(key, lock_res, search_res,
		                                          value);
#endif
		// assert(value != ValueNull);
		return (value == ValueNull);
	}

	bool from_cache = false;
	if(use_cache_mark) {
        std::vector<Record>& path = inner_tree[coro_id].path;
		dmcache->search_cache(key, inner_record, path);
		leaf_addr = inner_record.addr;
		from_cache = (leaf_addr != Gaddr::Null());
	}
	if(!from_cache) {
		inner_record = find_leaf(key, coro_id, ctx);
		leaf_addr = inner_record.addr;
	}
	leaf_search(leaf_addr, inner_record, from_cache, key, value, coro_id, ctx);
	// assert(value != ValueNull);
#ifdef ENABLE_READ_DELEGATION
	local_lock_table->release_local_read_lock(key, lock_res, search_res, value);
#endif
	return (value == ValueNull);
}

// public functions - write
void DMTree::write(const Key& key, Value& value, int coro_id,
                   CoroContext* ctx) {
	Gaddr leaf_addr = Gaddr::Null();
	Record inner_record;

	// write-combing handover
	bool write_handover = false;
	std::pair<bool, bool> lock_res = std::make_pair(false, false);
	lock_res.first = false;  //  is handover
	lock_res.second = false; // is conflicted

#ifdef ENABLE_WRITE_COMBINING
	lock_res =
	    local_lock_table->acquire_local_write_lock(key, value, ctx, coro_id);
	write_handover = (lock_res.first && !lock_res.second);
#endif

	if(write_handover) {
#ifdef ENABLE_WRITE_COMBINING
		local_lock_table->release_local_write_lock(key, lock_res);
#endif
		return;
	}

	bool from_cache = false;
	if(use_cache_mark) {
        std::vector<Record>& path = inner_tree[coro_id].path;
		dmcache->search_cache(key, inner_record, path);
		leaf_addr = inner_record.addr;
		from_cache = (leaf_addr != Gaddr::Null());
	}
	if(!from_cache) {
		inner_record = find_leaf(key, coro_id, ctx);
		leaf_addr = inner_record.addr;
	}
	// std::cout << "find SubHash " << leaf_addr.offset << " " <<
	// inner_record.capacity << std::endl;
	leaf_insert(leaf_addr, inner_record, from_cache, key, value, coro_id, ctx);

#ifdef ENABLE_WRITE_COMBINING
	local_lock_table->release_local_write_lock(key, lock_res);
#endif
}

// public functions - scan
int DMTree::scan(const Key& from, const Key& to, int len, int coro_id,
                 CoroContext* ctx) {
	Gaddr leaf_addr = Gaddr::Null();
	Record inner_record;

	bool from_cache = false;
	if(use_cache_mark) {
        std::vector<Record>& path = inner_tree[coro_id].path;
		dmcache->search_cache(from, inner_record, path);
		leaf_addr = inner_record.addr;
		from_cache = (leaf_addr != Gaddr::Null());
	}
	if(!from_cache) {
		inner_record = find_leaf(from, coro_id, ctx);
		leaf_addr = inner_record.addr;
	}
	EntrySortQueue kv_results;
	leaf_scan(leaf_addr, inner_record, from_cache, from, to, len, kv_results,
	          coro_id, ctx);
	if(kv_results.size() != len) {
		if(kv_results.size() < len) {
			std::cout << "less than" << kv_results.size() << " " << len
			          << std::endl;
		}
	}
	return 0;
}

// sorted meta can be constructed in background
void DMTree::background_sort_meta(Gaddr addr, int capacity, int coro_id,
                                  CoroContext* ctx) {}