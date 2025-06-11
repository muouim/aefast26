#include "DMTree.h"

// init root node
void DMTree::init_root_node() {
	// try to init root node
	Gaddr groot_addr(0, define::kRootPointerStoreOffest);
	auto cas_buffer = (dmv->get_rbuf(0)).get_cas_buffer();
	auto root_inner_addr = dmv->memory_alloc(InnerNodeSize);

	if(!dmv->cas_sync(groot_addr, 0, root_inner_addr.val, cas_buffer)) {
		return;
	}
	std::cout << "root node offset: " << root_inner_addr.offset << std::endl;

	// alloc and init head leaf node
	auto buffer = (dmv->get_rbuf(0)).get_page_buffer();
	auto& leaf_node = leaf_nodes[0];
	leaf_node.attach_node(LeafNodeCapacity);

	auto root_leaf_addr = dmv->memory_alloc(leaf_node.cur_node_size());
	std::cout << "head leaf offset: " << root_leaf_addr.offset << std::endl;

	// alloc and init head leaf fp table
	FPTable* fp_table = new FPTable(buffer, leaf_node.cur_capacity());
	memset(buffer, 0, fp_table->get_size());
	fp_table->set_consistent();

	auto root_fp_addr = dmv->memory_alloc(fp_table->get_size());
	dmv->write_sync(buffer, root_fp_addr, fp_table->get_size());

	memset(buffer, 0, leaf_node.cur_node_size());
	LeafHeader root_leaf_hdr;
	root_leaf_hdr.capacity = LeafNodeCapacity;
	root_leaf_hdr.right_addr = Gaddr::Null();
	root_leaf_hdr.right_capacity = 0;
	*(LeafHeader*)buffer = root_leaf_hdr;

	for(int i = 0; i < leaf_node.cur_capacity(); i++) {
		auto e_offset = leaf_node.get_entry_offset(i);
		auto entry = (Entry*)(buffer + e_offset);
		entry->set_consistent();
	}
	dmv->write_sync(buffer, root_leaf_addr, leaf_node.cur_node_size());

	memset(buffer, 0, InnerNodeSize);
	auto node = inner_tree[0].node;

	InnerHeader root_node_hdr;
	root_node_hdr.is_root = true;
	root_node_hdr.next_is_leaf = true;

	node = (InnerNode*)(buffer);
	node->set_hdr(root_node_hdr);

	Record init_record(KeyNull, root_leaf_addr, root_fp_addr, LeafNodeCapacity,
	                   0);
	node->insert(init_record);
	dmv->write_sync(buffer, root_inner_addr, InnerNodeSize);

	init_record.addr = root_inner_addr;
	dmcache->insert_cache(init_record, node);
	delete fp_table;
}

// get root addr
void DMTree::get_root_addr(Gaddr& root_addr, int coro_id, CoroContext* ctx) {

	if(inner_tree[coro_id].local_root_addr != Gaddr::Null()) {
		root_addr = inner_tree[coro_id].local_root_addr;
		return;
	}

	Gaddr groot_addr(0, define::kRootPointerStoreOffest);

	auto buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();
	dmv->read_sync((char*)buffer, groot_addr, sizeof(uint64_t), ctx);

	root_addr = *(Gaddr*)buffer;
	inner_tree[coro_id].local_root_addr = root_addr;
	assert(root_addr != Gaddr::Null());
}

// search inner index
bool DMTree::inner_search(Gaddr addr, const Key& key, Record& record,
                          bool& next_is_leaf, int coro_id, CoroContext* ctx) {

	auto& node = inner_tree[coro_id].node;
	std::vector<Record>& path = inner_tree[coro_id].path;

	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();
inner_node_reread:
	dmv->read_sync(buffer, addr, InnerNodeSize, ctx);
	node = (InnerNode*)buffer;

	if(!node->check_consistent()) {
		goto inner_node_reread;
	}

	if(path.size() == 1 && node->get_is_root() != true) {
		inner_tree[coro_id].local_root_addr = Gaddr::Null();
		get_root_addr(addr, coro_id, ctx);
		path[0].addr = addr;
		goto inner_node_reread;
	}

	if(!node->check_hdr(key)) {
		// std::cout << key << " right sibling " << node->get_hdr().max_key
		//           << std::endl;

		record.addr = node->get_hdr().right_addr;
		next_is_leaf = node->get_hdr().next_is_leaf;
		assert(record.addr != Gaddr::Null());

		path[path.size() - 1].addr = record.addr;
		return false;
	}
	node->search(key, record, next_is_leaf);

	if(use_cache_mark && next_is_leaf) {
		dmcache->insert_cache(path[path.size() - 1], node);
	}
	return true;
}

// find to leaf
Record DMTree::find_leaf(const Key& key, int coro_id, CoroContext* ctx) {

	Gaddr addr = Gaddr::Null();
	get_root_addr(addr, coro_id, ctx);

	Record record(KeyNull, addr, Gaddr::Null(), 0, 0);

	std::vector<Record>& path = inner_tree[coro_id].path;
	path.clear();
	path.push_back(record);
	bool next_is_leaf = false;

	while(true) {
		bool to_next =
		    inner_search(addr, key, record, next_is_leaf, coro_id, ctx);
		addr = record.addr;
		if(!to_next) {
			continue;
		}
		path.push_back(record);
		if(next_is_leaf) {
			break;
		}
	}

	assert(record.capacity > 0);
	return record;
}

bool DMTree::bottom_search(Gaddr addr, const Key& key, Record& record,
                           bool& next_is_leaf, int coro_id, CoroContext* ctx) {

	auto& node = inner_tree[coro_id].node;
	std::vector<Record>& path = inner_tree[coro_id].path;

	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();

bottom_node_reread:
	dmv->read_sync(buffer, addr, InnerNodeSize, ctx);
	node = (InnerNode*)buffer;

	if(!node->check_consistent()) {
		goto bottom_node_reread;
	}

	if(!node->check_hdr(key)) {
		// std::cout << key << " right sibling " << node->get_hdr().max_key
		//          << std::endl;

		record.addr = node->get_hdr().right_addr;
		next_is_leaf = node->get_hdr().next_is_leaf;
		assert(record.addr != Gaddr::Null());

		path[path.size() - 1].addr = record.addr;
		return false;
	}
	node->search(key, record, next_is_leaf);
	assert(next_is_leaf);

	if(use_cache_mark && next_is_leaf) {
		path[path.size() - 1].key = node->get_hdr().min_key;
		path[path.size() - 1].addr = addr;
		dmcache->insert_cache(path[path.size() - 1], node);
	}
	return true;
}

Record DMTree::read_bottom_inner(const Key& key, int coro_id,
                                 CoroContext* ctx) {

	std::vector<Record>& path = inner_tree[coro_id].path;
	assert(path.size() == 2);

	Gaddr addr = path[0].addr;
	Record record(KeyNull, addr, Gaddr::Null(), 0, 0);
	path.clear();
	path.push_back(record);

	bool next_is_leaf = false;
	while(true) {
		bool to_next =
		    bottom_search(addr, key, record, next_is_leaf, coro_id, ctx);
		addr = record.addr;
		if(!to_next) {
			continue;
		}
		path.push_back(record);
		assert(next_is_leaf);
		break;
	}

	assert(record.capacity > 0);
	return record;
}

// lock inner node
void DMTree::lock_node(Gaddr addr, uint64_t lock_offset, int coro_id,
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

// unlock inner node
void DMTree::unlock_node(Gaddr addr, uint64_t lock_offset, int coro_id,
                         CoroContext* ctx) {

	auto buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();

	Gaddr lock_addr(addr.nodeID, addr.offset + lock_offset);

	*buffer = 0;
	dmv->write_sync((char*)buffer, lock_addr, sizeof(uint64_t), ctx);
}

// index insertion
bool DMTree::inner_write(Gaddr &addr, Record& inner_record,
                         Record& update_record, bool is_bottom, int coro_id,
                         CoroContext* ctx) {

	auto& node = inner_tree[coro_id].node;

	auto buffer = (dmv->get_rbuf(coro_id)).get_page_buffer();

	lock_node(addr, node->get_lock_offset(), coro_id, ctx);

	dmv->read_sync(buffer, addr, InnerNodeSize, ctx);
	node = (InnerNode*)buffer;
	assert(node->check_consistent());

	// turn to right inner block
	while(!node->check_hdr(inner_record.key)) {
		// std::cout << inner_record.key << "inner write right " << node->get_hdr().max_key
		//          << std::endl;
		unlock_node(addr, node->get_lock_offset(), coro_id, ctx);
		addr = node->get_hdr().right_addr;

		lock_node(addr, node->get_lock_offset(), coro_id, ctx);
		dmv->read_sync(buffer, addr, InnerNodeSize, ctx);
		node = (InnerNode*)buffer;

		assert(node->check_consistent());
	}
	// insert new entry
	if(is_bottom) {
		assert(node->get_hdr().next_is_leaf);
		assert(node->check_hdr(update_record.key));
		assert(node->insert(update_record));
	}
	if(node->insert(inner_record)) {
		node->unlock();
		dmv->write_sync(buffer, addr, InnerNodeSize, ctx);
		return true;
	}
	return false;
}

// inerternal split
void DMTree::inner_insert(const Key& key, Record& inner_record,
                          Record& update_record, bool& from_cache, int coro_id,
                          CoroContext* ctx) {

	std::vector<Record>& path = inner_tree[coro_id].path;

	bool is_bottom = true;
	for(int i = path.size() - 1; i > 0; i--) {
		Gaddr addr = path[i - 1].addr;
		if(inner_write(addr, inner_record, update_record, is_bottom, coro_id,
		               ctx)) {
			break;
		}
		auto& node = inner_tree[coro_id].node;
        path[i - 1].addr = addr;
        path[i - 1].key = node->get_hdr().min_key;

		if(from_cache) {
			unlock_node(addr, node->get_lock_offset(), coro_id, ctx);
			find_leaf(key, coro_id, ctx);
			from_cache = false;
			i = path.size();
			continue;
		}
		is_bottom = false;
		node_split(addr, inner_record, coro_id, ctx);

		if(i == 1) {
			update_root(inner_record, addr, coro_id, ctx);
		}
	}
}

// block split
void DMTree::node_split(Gaddr addr, Record& record, int coro_id,
                        CoroContext* ctx) {

	Record upper_record;
	std::vector<Record> records;

	auto node = inner_tree[coro_id].node;
	auto buffer = (char*)node;
	node->unlock();

	auto sibling_buffer = (dmv->get_rbuf(coro_id)).get_sibling_buffer();
	memset(sibling_buffer, 0, InnerNodeSize);

	InnerHeader origin_hdr = node->get_hdr();

	InnerHeader sibling_hdr = node->get_hdr();
	Gaddr sibling_addr = dmv->memory_alloc(InnerNodeSize);

	node->split(true, records, upper_record);
	upper_record.addr = sibling_addr;

	// old inner node
	origin_hdr.right_addr = sibling_addr;
	origin_hdr.max_key = upper_record.key;
	origin_hdr.is_root = false;
	node->set_hdr(origin_hdr);

	if(upper_record.key > record.key) {
		assert(node->insert(record));
	}

	// new inner node
	sibling_hdr.min_key = upper_record.key;
	sibling_hdr.is_root = false;

	node = (InnerNode*)sibling_buffer;
	node->set_hdr(sibling_hdr);
	node->split(false, records, upper_record);

	// write new block
	if(upper_record.key < record.key) {
		assert(node->insert(record));
	}

	dmv->write_sync(sibling_buffer, sibling_addr, InnerNodeSize, ctx);
	dmv->write_sync(buffer, addr, InnerNodeSize, ctx);

	record.key = upper_record.key;
	record.addr = upper_record.addr;
}

// update root index
void DMTree::update_root(Record& record, Gaddr update_addr, int coro_id,
                         CoroContext* ctx) {

	std::cout << "update root" << std::endl;

	auto node = inner_tree[coro_id].node;
	auto buffer = (dmv->get_rbuf(coro_id)).get_cas_buffer();
	std::vector<Record>& path = inner_tree[coro_id].path;

	auto sibling_buffer = (dmv->get_rbuf(coro_id)).get_sibling_buffer();
	memset(sibling_buffer, 0, InnerNodeSize);

	// lock the whole index
	Gaddr glock_addr(0, define::kLockStartAddr);
	lock_node(glock_addr, 0, coro_id, ctx);

	// read root
	Gaddr groot_addr(0, define::kRootPointerStoreOffest);
	dmv->read_sync((char*)buffer, groot_addr, sizeof(uint64_t), ctx);
	Gaddr root_addr = *(Gaddr*)buffer;
	assert(path[0].addr == root_addr);

	// init new root header
	InnerHeader root_hdr;
	root_hdr.is_root = true;
	root_hdr.next_is_leaf = false;

	// alloc new root space
	Gaddr sibling_addr = dmv->memory_alloc(InnerNodeSize);

	// write entries to new root
	node = (InnerNode*)sibling_buffer;
	node->set_hdr(root_hdr);

	Record init_record(KeyNull, update_addr, Gaddr::Null(), 0, 0);

	assert(node->insert(record) && node->insert(init_record));
	dmv->write_sync(sibling_buffer, sibling_addr, InnerNodeSize, ctx);

	// update the new address of root
	*buffer = (uint64_t)sibling_addr.val;
	dmv->write_sync((char*)buffer, groot_addr, sizeof(uint64_t), ctx);
	unlock_node(glock_addr, 0, coro_id, ctx);
}