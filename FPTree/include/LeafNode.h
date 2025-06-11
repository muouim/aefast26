#ifndef _LEAF_HASH_H_
#define _LEAF_HASH_H_

#include "KVConfig.h"
#include "RdmaConfig.h"
#include "third_party/hash.h"
#include <random>

// #define ENABLE_ENTRY_CRC
constexpr int LeafNodeCapacity = 32;
constexpr int MaxNodeCapacity = 128;

class Entry {
public:
	union {
		struct {
			uint64_t valid : 8;
			uint64_t version : 56;
		};
		uint64_t raw;
	};
	Key key;
	Value value;
#ifdef ENABLE_ENTRY_CRC
	uint64_t crc;
#endif

public:
	Entry() { valid = 0; }

	uint64_t get_version() { return version; }

	void add_version() {
		version++;
		set_consistent();
	}
	void set_version(uint64_t v) {
		version = v;
		set_consistent();
	}
	void set_consistent() {
#ifdef ENABLE_ENTRY_CRC
		size_t crc_size = sizeof(Entry) - sizeof(crc);
		crc = CityHash32((char*)&raw, crc_size);
#endif
	}

	bool check_consistent() {
#ifdef ENABLE_ENTRY_CRC
		size_t crc_size = sizeof(Entry) - sizeof(crc);
		uint64_t cal_crc = CityHash32((char*)&raw, crc_size);
		return ((unsigned)crc == (unsigned)cal_crc);
#else 
        return true;
#endif
	}

	// search key from entry
	bool search(const Key& k, Value& v) {

		if(this->valid == 1 && this->key == k) {
			v = this->value;
			return true;
		}

		v = ValueNull;
		return false;
	}

	// insert key into entry
	bool insert(const Key& k, const Value& v) {

		if(this->valid == 0) {
			this->key = k;
			this->value = v;
			this->valid = 1;
			set_consistent();
			return true;
		}

		if(this->key == k) {
			this->value = v;
			set_consistent();
			return true;
		}

		return false;
	}

	// insert key into entry
	void clean() {
		assert(this->valid == 1);

		this->valid = 0;
		memset(this->key.key, 0, KEY_SIZE);
		set_consistent();
	}

} __packed;

class EntryPos {
public:
	Key key;
	Value value;

	EntryPos() {}
	EntryPos(const Entry& e) {
		key = e.key;
		value = e.value;
	}
	EntryPos(const Key& k, const Value& v)
	    : key(k)
	    , value(v) {}
} __packed;

// sorted keys
struct entry_cmp {
	bool operator()(const EntryPos& a, const EntryPos& b) {
		return (a.key < b.key);
	}
};

typedef std::priority_queue<EntryPos, std::vector<EntryPos>, entry_cmp>
    EntrySortQueue;

// varing capacity FPTable
class FPTable {
public:
	uint64_t *version;
	uint8_t *fp;
	uint64_t *crc;
	uint64_t *lock;
	uint64_t capacity;

public:
	FPTable() {}

	FPTable(char *buffer, uint64_t c) {
        attach(buffer, c);
	}

    void attach(char *buffer, uint64_t c) {
        capacity = c;
        version = (uint64_t *)buffer;
        fp = (uint8_t *)((char *)version + sizeof(uint64_t));
        crc = (uint64_t *)((char *) fp + sizeof(uint8_t) * capacity);
        lock = (uint64_t *)((char *) crc + sizeof(uint64_t));
    }

    uint64_t get_size() {
        return sizeof(uint8_t) * capacity + sizeof(uint64_t) * 3;
    }

    uint64_t get_lock_offset() {
        return get_size() - sizeof(uint64_t);
    }

	int get_entry_num() {
        int entry_num = 0;
		for(int i = 0; i < capacity; i++) {
            if(fp[i] != 0) {
                entry_num++;
            }
        }
        return entry_num;
    }

	bool insert(uint8_t f, int& e_id) {
		for(int i = 0; i < capacity; i++) {
			if(fp[i] == 0) {
				fp[i] = f;
				e_id = i;
				set_consistent();
				return true;
			}
		}
		return false;
	}

	bool search(uint8_t f, int& e_id) {
		for(int i = 0; i < capacity; i++) {
			if(fp[i] == f) {
				e_id = i;
				return true;
			}
		}
		return false;
	}

	uint64_t get_version() { return *version; }

	void add_version() {
		*version++;
		set_consistent();
	}

	void set_version(uint64_t v) {
		*version = v;
		set_consistent();
	}

	void unlock() { *lock = 0ull; }

	void clean() {
		for(int i = 0; i < capacity; i++) {
			fp[i] = 0;
		}
		set_consistent();
	}

	void set_consistent() {
		size_t crc_size = get_size() - sizeof(uint64_t) * 2;
		*crc = CityHash32((char*)version, crc_size);
	}

	bool check_consistent() {
		size_t crc_size = get_size() - sizeof(uint64_t) * 2;
		uint64_t cal_crc = CityHash32((char*)version, crc_size);
		return (*(unsigned *)crc == (unsigned)cal_crc);
	}
} __packed;

class LeafHeader {
public:
	Key max_key;
	Key min_key;
	int capacity;
	int version;
	Gaddr right_addr;
	Gaddr right_fp_addr;
	uint64_t right_capacity;

	LeafHeader() {
		min_key = KeyNull;
		max_key = KeyMax;
		capacity = 0;
		version = 0;
		right_capacity = 0;
	}

	LeafHeader(const LeafHeader& h) {
		right_addr = h.right_addr;
		right_fp_addr = h.right_fp_addr;
		capacity = h.capacity;
		right_capacity = h.right_capacity;
		version = h.version;
		max_key = h.max_key;
		min_key = h.min_key;
	}

	void update_right(Gaddr r_a, Gaddr r_fp_a, uint64_t r_c) {
		right_addr = r_a;
		right_fp_addr = r_fp_a;
		right_capacity = r_c;
	}
} __packed;

class LeafNode {
private:
	uint64_t cur_node_capacity;

public:
	LeafNode() {}

	void attach_node(uint64_t capacity) {
		assert(capacity > 0);
		cur_node_capacity = capacity;
	}

	uint64_t cur_node_mag() { return cur_node_capacity / LeafNodeCapacity; }

	uint64_t cur_capacity() { return cur_node_capacity; }

	uint64_t cur_node_size() {
		return sizeof(LeafHeader) + cur_capacity() * (sizeof(Entry));
	}

	static uint8_t cal_key_fp(const Key& key) {
		uint32_t hash =
		    MurmurHash3_x86_32((char*)&key, sizeof(Key), kHashSeed[0]);
		hash = FNVHash64(hash) % UINT32_MAX;
		if((uint8_t)((hash >> 24) & UINT8_MAX) == 0) {
			return 1;
		}
		return (uint8_t)((hash >> 24) & UINT8_MAX);
	}

	uint64_t get_entry_offset(int e_id) {
		return sizeof(LeafHeader) + e_id * sizeof(Entry);
	}
};
#endif
