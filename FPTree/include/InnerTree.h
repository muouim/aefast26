#ifndef _INNER_TREE_H_
#define _INNER_TREE_H_

#include "KVConfig.h"
#include "RdmaConfig.h"

constexpr int InnerNodeCapacity = 32;

class Record;
class Record {
public:
	union {
		struct {
			uint64_t valid : 8;
			uint64_t version : 56;
		};
		uint64_t raw;
	};
	Key key;
	Gaddr addr;
	Gaddr fp_addr;
	int capacity;

	Record() {
		valid = 0;
		capacity = 0;
		version = 0;
	}

	Record(const Record& r) {
		key = r.key;
		addr = r.addr;
		fp_addr = r.fp_addr;
		capacity = r.capacity;
		version = r.version;
		valid = 1;
	}
	Record(const Key& k, const Gaddr& a, const Gaddr& f_a, const int& c,
	       const int& v)
	    : key(k)
	    , addr(a)
	    , fp_addr(f_a)
	    , capacity(c)
	    , version(v) {
		valid = 1;
	}
} __packed;

class InnerHeader {
public:
	Key max_key;
	Key min_key;
	Gaddr right_addr;
	bool is_root;
	bool next_is_leaf;
	uint8_t padding[2];

	InnerHeader() {
		max_key = KeyMax;
		is_root = false;
		next_is_leaf = false;
	}
	InnerHeader(const InnerHeader& h) {
		right_addr = h.right_addr;
		max_key = h.max_key;
		min_key = h.min_key;
		is_root = h.is_root;
		next_is_leaf = h.next_is_leaf;
	}
} __packed;

class InnerNode {
private:
	InnerHeader hdr;
	Record record[InnerNodeCapacity];
	uint8_t padding2[7];
	uint64_t crc;
	uint64_t lock;

	static bool cmp(Record a, Record b) { return a.key < b.key; };

	friend class Internal;

public:
	InnerNode() { lock = 0ull; }

	void set_consistent() {
		crc = CityHash32((char*)&hdr,
		                 sizeof(InnerNode) - sizeof(crc) - sizeof(lock));
	}

	bool check_consistent() {
		uint64_t cal_crc = CityHash32(
		    (char*)&hdr, sizeof(InnerNode) - sizeof(crc) - sizeof(lock));
		return ((unsigned)crc == (unsigned)cal_crc);
	}

	bool check_hdr(const Key& key) {
		return (hdr.max_key > key && hdr.min_key <= key);
	}

	const InnerHeader& get_hdr() { return hdr; }

	void set_hdr(const InnerHeader& h) {
		hdr = h;
		set_consistent();
	}

	bool get_is_root() { return hdr.is_root; }

	uint64_t get_lock_offset() { return offsetof(class InnerNode, lock); }

	void unlock() { lock = 0ull; }

	// TODO: it assumes that there is no delete operation
	void sort_node() {
		int count = 0;
		for(int i = 0; i < InnerNodeCapacity; i++) {
			if(record[i].valid == 1) {
				count++;
			}
		}
		assert(count > 0);
		if(count != 1) {
			std::sort(record, record + count, cmp);
		}
	}

	// search node for key, min_key point to child nodes
	void search(const Key& key, Record& path, bool& next_is_leaf) {
		int i = 0;
		for(i = 0; i < InnerNodeCapacity; i++) {
			if(record[i].valid == 0 || record[i].key > key) {
				break;
			}
		}
		path = record[i - 1];
		next_is_leaf = hdr.next_is_leaf;
	}

	// insert key into node
	bool insert(const Record& insert_record) {

		for(int i = 0; i < InnerNodeCapacity; i++) {

			if(record[i].valid == 0) {
				record[i] = insert_record;
				record[i].valid = 1;
				sort_node();
				set_consistent();
				return true;
			}
			if(record[i].key == insert_record.key) {
				record[i] = insert_record;
				set_consistent();
				return true;
			}
		}
		return false;
	}

	// split node if no space int the node
	bool split(bool origin, std::vector<Record>& mig_records,
	           Record& record_pos) {
		int half_id = InnerNodeCapacity / 2;

		if(origin == true) {
			record_pos = record[half_id];
			record[half_id].valid = 0;
			mig_records.push_back(record_pos);

			// half key is not stored in original node
			for(int i = half_id + 1; i < InnerNodeCapacity; i++) {
				Record mig_record = record[i];
				mig_records.push_back(mig_record);
				record[i].valid = 0;
			}
		} else {
			for(int i = 0; i < mig_records.size(); i++) {
				record[i] = mig_records[i];
				record[i].valid = 1;
			}
		}
		set_consistent();
		return true;
	}

	// fetch all keys in the node
	void scan(std::vector<Record>& all_records) {
		for(int i = 0; i < InnerNodeCapacity; i++) {
			if(record[i].valid == 0) {
				continue;
			}
			Record record_pos = record[i];
			all_records.push_back(record_pos);
		}
	}
};

constexpr int InnerNodeSize = sizeof(InnerNode);

class InnerTree {
private:
public:
	InnerTree() { local_root_addr = Gaddr::Null(); };
	~InnerTree(){};

	InnerNode* node;
	Gaddr local_root_addr;
	std::vector<Record> path;
	uint64_t size() { return sizeof(InnerNode); }
};

#endif
