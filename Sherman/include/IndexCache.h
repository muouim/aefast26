#if !defined(_INDEX_CACHE_H_)
#define _INDEX_CACHE_H_

#include "CacheEntry.h"
#include "HugePageAlloc.h"
#include "Timer.h"
#include "WRLock.h"
#include "third_party/inlineskiplist.h"
#include "third_party/lockfree_queue.h"

#include <atomic>
#include <queue>
#include <vector>

extern bool enter_debug;

using CacheSkipList = InlineSkipList<CacheEntryComparator>;

class IndexCache {

public:
  IndexCache(int cache_size);

  bool add_to_cache(InternalPage *page);
  const CacheEntry *search_from_cache(const Key &k, GlobalAddress *addr,
                                      bool is_leader = false);

  void search_range_from_cache(const Key &from, const Key &to,
                               std::vector<InternalPage *> &result);

  bool add_entry(const Key &from, const Key &to, InternalPage *ptr);
  const CacheEntry *find_entry(const Key &k);
  const CacheEntry *find_entry(const Key &from, const Key &to);

  bool invalidate(const CacheEntry *entry);

  const CacheEntry *get_a_random_entry(uint64_t &freq);

  void statistics();

  void bench();

private:
  uint64_t cache_size; // MB;
  std::atomic<int64_t> free_page_cnt;
  std::atomic<int64_t> skiplist_node_cnt;
  int64_t all_page_cnt;

  LockFreeQueue<InternalPage *> cache_pool_list;
  void* cache_buffer;
  WRLock free_lock;

  // SkipList
  CacheSkipList *skiplist;
  CacheEntryComparator cmp;
  Allocator alloc;

  void evict_one();
};

inline IndexCache::IndexCache(int cache_size) : cache_size(cache_size) {
  skiplist = new CacheSkipList(cmp, &alloc, 21);
  uint64_t memory_size = define::MB * cache_size;
  cache_buffer = hugePageAlloc(memory_size);
  printf("Cache %d MB\n", cache_size);

  all_page_cnt = memory_size / kInternalPageSize;
  for(uint64_t i = 0; i < all_page_cnt; i++) {
    cache_pool_list.push((InternalPage *)(cache_buffer + uint64_t(i * kInternalPageSize)));
  }
  free_page_cnt.store(all_page_cnt);
  skiplist_node_cnt.store(0);
}

// [from, to）
inline bool IndexCache::add_entry(const Key &from, const Key &to,
                                  InternalPage *ptr) {

  // TODO memory leak
  auto buf = skiplist->AllocateKey(sizeof(CacheEntry));
  auto &e = *(CacheEntry *)buf;
  e.from = from;
  e.to = to - 1; // !IMPORTANT;
  e.ptr = ptr;

  return skiplist->InsertConcurrently(buf);
}

inline const CacheEntry *IndexCache::find_entry(const Key &from,
                                                const Key &to) {
  CacheSkipList::Iterator iter(skiplist);

  CacheEntry e;
  e.from = from;
  e.to = to - 1;
  iter.Seek((char *)&e);
  if (iter.Valid()) {
    auto val = (const CacheEntry *)iter.key();
    return val;
  } else {
    return nullptr;
  }
}

inline const CacheEntry *IndexCache::find_entry(const Key &k) {
  return find_entry(k, k + 1);
}

inline bool IndexCache::add_to_cache(InternalPage *page) {
  InternalPage* new_page;
  while(cache_pool_list.pop(new_page) == false) {
    evict_one();
  }

  memcpy(new_page, page, kInternalPageSize);
  new_page->index_cache_freq = 0;

  if (this->add_entry(page->hdr.lowest, page->hdr.highest, new_page)) {
    skiplist_node_cnt.fetch_add(1);
    auto v = free_page_cnt.fetch_add(-1);
    if (v <= 0) {
      evict_one();
    }

    return true;
  } else { // conflicted
    auto e = this->find_entry(page->hdr.lowest, page->hdr.highest);
    if (e && e->from == page->hdr.lowest && e->to == page->hdr.highest - 1) {
      auto ptr = e->ptr;
      if (ptr == nullptr &&
          __sync_bool_compare_and_swap(&(e->ptr), 0ull, new_page)) {
        auto v = free_page_cnt.fetch_add(-1);
        if (v <= 0) {
          evict_one();
        }
        return true;
      }
    }

    cache_pool_list.push(new_page);
    return false;
  }
}

inline const CacheEntry *IndexCache::search_from_cache(const Key &k,
                                                       GlobalAddress *addr,
                                                       bool is_leader) {
  auto entry = find_entry(k);

  InternalPage *page = entry ? entry->ptr : nullptr;

  if (page && entry->from <= k && entry->to >= k) {

    page->index_cache_freq++;

    auto cnt = page->hdr.last_index + 1;
    if (k < page->records[0].key) {
      *addr = page->hdr.leftmost_ptr;
    } else {

      bool find = false;
      for (int i = 1; i < cnt; ++i) {
        if (k < page->records[i].key) {
          find = true;
          *addr = page->records[i - 1].ptr;
          break;
        }
      }
      if (!find) {
        *addr = page->records[cnt - 1].ptr;
      }
    }

    compiler_barrier();
    if (entry->ptr) { // check if it is freed.
      return entry;
    }
  }

  return nullptr;
}

inline void
IndexCache::search_range_from_cache(const Key &from, const Key &to,
                                    std::vector<InternalPage *> &result) {
  CacheSkipList::Iterator iter(skiplist);

  result.clear();
  CacheEntry e;
  e.from = from;
  e.to = from;
  iter.Seek((char *)&e);

  while (iter.Valid()) {
    auto val = (const CacheEntry *)iter.key();
    if (val->ptr) {
      if (val->from > to) {
        return;
      }
      result.push_back(val->ptr);
    }
    iter.Next();
  }
}

inline bool IndexCache::invalidate(const CacheEntry *entry) {
  auto ptr = entry->ptr;

  if (ptr == nullptr) {
    return false;
  }

  if (__sync_bool_compare_and_swap(&(entry->ptr), ptr, 0)) {

    cache_pool_list.push(ptr);
    free_page_cnt.fetch_add(1);
    return true;
  }

  return false;
}

inline const CacheEntry *IndexCache::get_a_random_entry(uint64_t &freq) {
  uint32_t seed = asm_rdtsc();
  GlobalAddress tmp_addr;
retry:
  auto evict_id = rand_r(&seed) % all_page_cnt;
  auto cache_ptr = (InternalPage *)(cache_buffer + evict_id * kInternalPageSize);
  auto k = cache_ptr->hdr.lowest;
  auto e = this->search_from_cache(k, &tmp_addr);
  if (!e) {
    goto retry;
  }
  auto ptr = e->ptr;
  if (!ptr) {
    goto retry;
  }

  freq = ptr->index_cache_freq;
  if (e->ptr != ptr) {
    goto retry;
  }
  return e;
}

inline void IndexCache::evict_one() {
re_evict:
  uint64_t freq1, freq2;
  auto e1 = get_a_random_entry(freq1);
  auto e2 = get_a_random_entry(freq2);
  
  bool finish_evict = false;
  if (freq1 < freq2) {
    finish_evict = invalidate(e1);
  } else {
    finish_evict = invalidate(e2);
  }
  if(finish_evict == false) {
    goto re_evict;
  }
}

inline void IndexCache::statistics() {
  printf("[skiplist node: %ld]  [page cache: %ld, size: %ld] [all cache: %ld, size: %ld]\n", 
                    skiplist_node_cnt.load(),
        all_page_cnt - free_page_cnt.load(), 
        (all_page_cnt - free_page_cnt.load()) * kInternalPageSize,
        all_page_cnt, 
        (all_page_cnt) * kInternalPageSize);
}

inline void IndexCache::bench() {

  Timer t;
  t.begin();
  const int loop = 100000;

  /*
  for (int i = 0; i < loop; ++i) {
    uint64_t r = rand() % (5 * define::MB);
    this->find_entry(r);
  }

  t.end_print(loop);
  */
}

#endif // _INDEX_CACHE_H_
