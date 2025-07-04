#if !defined(_TREE_H_)
#define _TREE_H_

#include "DSM.h"
#include "Common.h"
#include "LocalLockTable.h"
#include "RolexCache.h"
#include "IdxCache.h"
#include "MetadataManager.h"
#include "LeafVersionManager.h"
#include "VersionManager.h"

#include <atomic>
#include <city.h>
#include <functional>
#include <map>
#include <algorithm>
#include <queue>
#include <set>
#include <iostream>


/* Workloads */
enum RequestType : int {
  INSERT = 0,
  UPDATE,
  SEARCH,
  SCAN
};

struct Request {
  RequestType req_type;
  Key k;
  Value v;
  int range_size;
};


class RequstGen {
public:
  RequstGen() = default;
  virtual Request next() { return Request{}; }
};


/* Rolex */
using GenFunc = std::function<RequstGen *(DSM*, Request*, int, int, int)>;

class RolexIndex {
public:
  using VerMng = VersionManager<LeafNode, LeafEntry>;
  RolexIndex(DSM *dsm, std::vector<Key> &load_keys, uint16_t rolex_id = 0);

  using WorkFunc = std::function<void (RolexIndex *, const Request&, CoroPull *)>;
  void run_coroutine(GenFunc gen_func, WorkFunc work_func, int coro_cnt, Request* req = nullptr, int req_num = 0);

  void insert(const Key &k, Value v, CoroPull* sink = nullptr);   // NOTE: insert can also do update things if key exists
  void update(const Key &k, Value v, CoroPull* sink = nullptr);   // assert(false) if key is not found
  bool search(const Key &k, Value &v, CoroPull* sink = nullptr);  // return false if key is not found
  void range_query(const Key &from, const Key &to, std::map<Key, Value> &ret);
  void statistics();
  void clear_debug_info();
  static thread_local std::vector<CoroPush> workers;
  static thread_local CoroQueue busy_waiting_queue;

private:
  // common
  void before_operation(CoroPull* sink);
  GlobalAddress get_leaf_address(uint64_t leaf_idx);

  // high-level functions
  std::tuple<bool, GlobalAddress, GlobalAddress, int> _search(const Key &k, Value &v, CoroPull* sink);  // return (key_is_found, leaf_addr, locked_leaf_addr)

  // low-level functions
  GlobalAddress insert_into_syn_leaf_locally(const Key &k, Value v, LeafNode*& syn_leaf, int nodeID, CoroPull* sink);  // return syn_addr if allocating a new synonym leaf
  void fetch_node(const GlobalAddress& leaf_addr, LeafNode*& leaf, CoroPull* sink, bool update_local_slt=true);
  void fetch_nodes(const std::vector<GlobalAddress>& leaf_addrs, std::vector<LeafNode*>& leaves, CoroPull* sink, bool update_local_slt=true);
  void write_node_and_unlock(const GlobalAddress& leaf_addr, LeafNode* leaf, const GlobalAddress& locked_leaf_addr, uint64_t* lock_buffer, CoroPull* sink);
  void write_nodes_and_unlock(const std::vector<GlobalAddress>& leaf_addrs, const std::vector<LeafNode*>& leaves, const GlobalAddress& locked_leaf_addr, uint64_t* lock_buffer, CoroPull* sink);

  // lock
  static uint64_t get_lock_info();
  void lock_node(const GlobalAddress &node_addr, uint64_t* lock_buffer, CoroPull* sink);
  void unlock_node(const GlobalAddress &node_addr, uint64_t* lock_buffer, CoroPull* sink, bool async = false);

  // hopscotch
#ifdef HOPSCOTCH_LEAF_NODE
  bool hopscotch_insert_and_unlock(LeafNode* leaf, const Key& k, Value v, const GlobalAddress& node_addr, uint64_t* lock_buffer, CoroPull* sink, int entry_num=define::leafSpanSize, bool is_locked_leaf=true);
  void hopscotch_split_and_unlock(LeafNode* leaf, const Key& k, Value v, const GlobalAddress& node_addr, uint64_t* lock_buffer, CoroPull* sink);
  Key hopscotch_get_split_key(LeafEntry* records, const Key& k);
  void hopscotch_insert_locally(LeafEntry* records, const Key& k, Value v);

  void hopscotch_fetch_node(const GlobalAddress& leaf_addr, int hash_idx, LeafNode*& leaf, CoroPull* sink, int entry_num=define::neighborSize, bool update_local_slt=true, bool need_metadata=true);
  void hopscotch_fetch_nodes(const std::vector<GlobalAddress>& leaf_addrs, int hash_idx, std::vector<LeafNode*>& leaves, CoroPull* sink, std::vector<int> entry_nums, bool update_local_slt=true, bool need_metadata=true);

  void segment_write_and_unlock(LeafNode* leaf, int l_idx, int r_idx, const std::vector<int>& hopped_idxes, const GlobalAddress& node_addr, uint64_t* lock_buffer, CoroPull* sink, bool need_unlock=true);
  void entry_write_and_unlock(LeafNode* leaf, const int idx, const GlobalAddress& node_addr, const GlobalAddress& locked_leaf_addr, uint64_t* lock_buffer, CoroPull* sink);
#endif

  // speculative read
#ifdef SPECULATIVE_READ
  bool speculative_read(GlobalAddress& leaf_addr, std::pair<int, int> range, const Key &k, Value &v, LeafNode*& leaf, int& speculative_idx, int& read_leaf_cnt, CoroPull* sink);
  void leaf_entry_read(const GlobalAddress& leaf_addr, const int idx, char *raw_leaf_buffer, char *leaf_buffer, CoroPull* sink);
#endif

  // coroutine
  void coro_worker(CoroPull &sink, RequstGen *gen, WorkFunc work_func);

private:
  DSM *dsm;
  RolexCache* rolex_cache;
#ifdef SPECULATIVE_READ
  IdxCache *idx_cache;
#endif
  LocalLockTable *local_lock_table;

  static thread_local std::map<GlobalAddress, GlobalAddress> coro_syn_leaf_addrs[MAX_CORO_NUM+1];

  uint64_t rolex_id;
};


#endif // _TREE_H_
