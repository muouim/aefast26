#if !defined(_ROLEX_CACHE_H_)
#define _ROLEX_CACHE_H_

#include "HugePageAlloc.h"
#include "Timer.h"
#include "DSM.h"

#include "rolex_libs/rolex/leaf_allocator.hpp"
#include "rolex_libs/rolex/model_allocator.hpp"
#include "rolex_libs/rolex/leaf_table.hpp"
#include "rolex_libs/rolex/rolex.hpp"
#include "rolex_libs/rolex/learned_cache.hpp"
#include "rolex_libs/rolex/remote_memory.hh"

#include <queue>
#include <atomic>
#include <vector>

#define USE_INT_128

using leaf_alloc_t = rolex::LeafAllocator<LeafNode, sizeof(LeafNode)>;
#ifdef USE_INT_128
using model_alloc_t = rolex::ModelAllocator<__uint128_t>;
using remote_memory_t = rolex::RemoteMemory<leaf_alloc_t, model_alloc_t>;
using leaf_table_t = rolex::LeafTable<__uint128_t, Value, LeafNode, leaf_alloc_t>;
using rolex_t = rolex::Rolex<__uint128_t, Value, LeafNode, leaf_alloc_t, remote_memory_t, define::epsilon>;
#else
using model_alloc_t = rolex::ModelAllocator<uint64_t>;
using remote_memory_t = rolex::RemoteMemory<leaf_alloc_t, model_alloc_t>;
using leaf_table_t = rolex::LeafTable<uint64_t, Value, LeafNode, leaf_alloc_t>;
using rolex_t = rolex::Rolex<uint64_t, Value, LeafNode, leaf_alloc_t, remote_memory_t, define::epsilon>;
#endif


class RolexCache {

public:
  RolexCache(DSM* dsm, std::vector<Key> &load_keys);

  std::pair<int, int> search_from_cache(const Key &k);
  std::tuple<int, int, int> search_from_cache_for_insert(const Key &k);
  std::pair<int, int> search_range_from_cache(const Key &from, const Key &to);
  void statistics();

    bool save_int_keys_to_file(const std::vector<__uint128_t>& int_keys, const std::string& filename) {  
        std::ofstream outfile(filename, std::ios::binary);
        if (!outfile) {  
            std::cerr << "Failed to open file for writing." << std::endl;  
            return false;  
        }  

        size_t size = int_keys.size();  
        outfile.write(reinterpret_cast<const char*>(&size), sizeof(size));  // 写入大小  

        for (const auto& key : int_keys) {  
            outfile.write(reinterpret_cast<const char*>(&key), sizeof(key));  
        }
        outfile.close();  
        return true;  
    }  

    bool load_int_keys_from_file(std::vector<__uint128_t>& int_keys, const std::string& filename) {  
        std::ifstream infile(filename, std::ios::binary);
        if (!infile) {  
            std::cerr << "Failed to open file for reading." << std::endl;  
            return false;  
        }  

        size_t size;  
        infile.read(reinterpret_cast<char*>(&size), sizeof(size));  

        int_keys.clear();  
        int_keys.reserve(size);

        for (size_t i = 0; i < size; ++i) {  
            __uint128_t key;  
            infile.read(reinterpret_cast<char*>(&key), sizeof(key));  
            int_keys.push_back(key);  
        }  

        infile.close();  
        return true;  
    }  

private:
  DSM *dsm;
  rolex_t* rolex_model;
#ifdef USE_INT_128
  std::vector<__uint128_t> int_keys;
#else
  std::vector<uint64_t> int_keys;
#endif
};

inline RolexCache::RolexCache(DSM* dsm, std::vector<Key> &load_keys) : dsm(dsm) {
  // processing data
  bool load_from_file = load_int_keys_from_file(int_keys, "load_keys.data");

if(!load_from_file) {
#ifdef USE_INT_128
  for (const auto& k : load_keys) int_keys.emplace_back(key2int128(k));
#else
  for (const auto& k : load_keys) int_keys.emplace_back(key2int(k));
#endif
  std::vector<Key>().swap(load_keys);

  std::cout<<"sort train data 1"<<std::endl;
  std::sort(int_keys.begin(), int_keys.end());
  int_keys.erase(std::unique(int_keys.begin(), int_keys.end()), int_keys.end());
  std::sort(int_keys.begin(), int_keys.end());
  save_int_keys_to_file(int_keys, "load_keys.data");
}
  for(int i = 1; i < int_keys.size(); ++ i){
    assert(int_keys[i] >= int_keys[i - 1]);
  }

  // initial local models
  rolex::RCtrl* ctrl = new RCtrl(define::fakePort);
  rolex::RM_config conf(ctrl, define::modelRegionSize, define::fakeLeafRegionSize, define::fakeRegLeafRegion);
  remote_memory_t* RM = new remote_memory_t(conf);
  rolex_model = new rolex_t(RM, int_keys);
  // rolex_model->print_data();
}


inline std::pair<int, int> RolexCache::search_from_cache(const Key &k) {  // [l, r]
#ifdef USE_INT_128
  auto key = key2int128(k);
#else
  auto key = key2int(k);
#endif
  auto [l, r, leaf_idx_offset, _] = rolex_model->get_leaf_range(key);
  return std::make_pair(leaf_idx_offset + l, leaf_idx_offset + r);
}


inline std::tuple<int, int, int> RolexCache::search_from_cache_for_insert(const Key &k) {  // [l, r, insert_leaf_idx]
#ifdef USE_INT_128
  auto key = key2int128(k);
#else
  auto key = key2int(k);
#endif
  auto [l, r, leaf_idx_offset, capacity_offset] = rolex_model->get_leaf_range(key);
  int global_key_idx = std::lower_bound(int_keys.begin(), int_keys.end(), key) - int_keys.begin();
  int insert_idx = (global_key_idx - capacity_offset) / define::leafSpanSize;
  // std::cout<<"insert idx "<<global_key_idx<<" "<<capacity_offset<<" "<<define::leafSpanSize<<std::endl;
  // std::cout<<"cache find leaves "<<insert_idx<<" "<<l<<" "<<r<<std::endl;
  // assert(insert_idx >= l && insert_idx <= r);
  if(!(insert_idx >= l && insert_idx <= r)) {
    std::cout<<"predict error "<<insert_idx<<" "<<l<<" "<<r<<std::endl;
    if(insert_idx < l) {
        l=insert_idx;
    }
    if(insert_idx > r) {
        r=insert_idx;
    }
  }
  // std::cout<<"cache 2"<<std::endl;
  return std::make_tuple(leaf_idx_offset + l, leaf_idx_offset + r, leaf_idx_offset + insert_idx);
}


inline std::pair<int, int> RolexCache::search_range_from_cache(const Key &from, const Key &to) {  // [l, r]
#ifdef USE_INT_128
  auto from_key = key2int128(from);
  auto to_key = key2int128(to);
#else
  auto from_key = key2int(from);
  auto to_key = key2int(to);
#endif
  auto [l, _1, leaf_idx_offset_from, _2]  = rolex_model->get_leaf_range(from_key);
  auto [_3, r, leaf_idx_offset_to  , _4] = rolex_model->get_leaf_range(to_key);
  return std::make_pair(leaf_idx_offset_from + l, leaf_idx_offset_to + r);
}

inline void RolexCache::statistics() {
  printf(" ----- [RolexCache] ----- \n");
  auto cache_size = rolex_model->get_consumed_cache_size();
  printf("consumed cache size = %.3lf MB\n", cache_size);
}

#endif // _ROLEX_CACHE_H_
