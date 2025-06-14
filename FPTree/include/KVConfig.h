#ifndef _STRUCTURE_H_
#define _STRUCTURE_H_

#include <algorithm>
#include <atomic>
#include <boost/crc.hpp>
#include <cassert>
#include <city.h>
#include <cstring>
#include <functional>
#include <iostream>
#include <queue>
#include <vector>

#define USE_SORTED_META
#define USE_HASH_RESIZING
#define ENABLE_WRITE_COMBINING
#define ENABLE_READ_DELEGATION
// #define ENABLE_HOCL

#define CUCKOO_INDEX_MARK 1
#define DMTREE_INDEX_MARK 2

#define ENABLE_CACHE true

constexpr int KEY_SIZE = 24;
constexpr int VALUE_SIZE = 8;

#define __packed __attribute__((packed))

class KeyType {
public:
	char key[KEY_SIZE];

	KeyType() { memset(key, 0, KEY_SIZE); }
	KeyType(const char* k) { memcpy(key, k, KEY_SIZE); }
	KeyType(const KeyType& k) { memcpy(key, k.key, KEY_SIZE); }
} __packed;

struct ValueType {
public:
	uint64_t value;

	ValueType() { value = 0; }
	ValueType(const uint64_t& v) { value = v; }
	ValueType(const ValueType& v) { value = v.value; }

	bool operator!=(const ValueType& a) { return value != a.value; }
	bool operator==(const ValueType& a) { return value == a.value; }
} __packed;

using Key = KeyType;
using Value = ValueType;

inline std::ostream& operator<<(std::ostream& os, const Key& obj) {
	os << obj.key;
	return os;
}

inline std::ostream& operator<<(std::ostream& os, const Value& obj) {
	os << obj.value;
	return os;
}

inline Key str2key(std::string &k) {
	Key key(k.c_str());
	return key;
}

// null value
const Key KeyNull;
const Key KeyMax("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
const Value ValueNull = {0};

inline bool operator<(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) < 0);
}
inline bool operator>(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) > 0);
}
inline bool operator==(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) == 0);
}
inline bool operator>=(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) >= 0);
}
inline bool operator<=(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) <= 0);
}
inline bool operator!=(const Key& a, const Key& b) {
	return (memcmp(a.key, b.key, KEY_SIZE) != 0);
}

#define forceinline inline __attribute__((always_inline))

static inline unsigned long long asm_rdtsc(void) {
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

__inline__ unsigned long long rdtsc(void) {
	unsigned hi, lo;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

inline void mfence() { asm volatile("mfence" ::: "memory"); }

inline void compiler_barrier() { asm volatile("" ::: "memory"); }

#endif
