#if !defined(_KEY_H_)
#define _KEY_H_

#include "Common.h"


inline uint8_t get_partial(const Key& key, int depth) {
  return depth == 0 ? 0 : key.at(depth - 1);
}


inline Key get_leftmost(const Key& key, int depth) {
  Key res{};
  std::copy(key.begin(), key.begin() + depth, res.begin());
  return res;
}


inline Key get_rightmost(const Key& key, int depth) {
  Key res{};
  std::copy(key.begin(), key.begin() + depth, res.begin());
  std::fill(res.begin() + depth, res.end(), (1UL << 8) - 1);
  return res;
}


using Prefix = std::vector<uint8_t>;
inline Key get_leftmost(const Prefix& prefix) {
  Key res{};
  std::copy(prefix.begin(), prefix.end(), res.begin());
  return res;
}


inline Key get_rightmost(const Prefix& prefix) {
  Key res{};
  std::copy(prefix.begin(), prefix.end(), res.begin());
  std::fill(res.begin() + prefix.size(), res.end(), (1UL << 8) - 1);
  return res;
}


inline Key remake_prefix(const Key& key, int depth, uint8_t diff_partial) {
  Key res{};
  if (depth > 0) {
    std::copy(key.begin(), key.begin() + depth - 1, res.begin());
    res.at(depth - 1) = diff_partial;
  }
  return res;
}


inline int longest_common_prefix(const Key &k1, const Key &k2, int depth) {
  assert((uint32_t)depth <= define::keyLen);

  int idx, max_cmp = define::keyLen - depth;

  for (idx = 0; idx <= max_cmp; ++ idx) {
    if (get_partial(k1, depth + idx) != get_partial(k2, depth + idx))
      return idx;
  }
  return idx;
}

inline void add_one(Key& a) {
  for (int i = 0; i < (int)define::keyLen; ++ i) {
    auto& partial = a.at(define::keyLen - 1 - i);
    if ((int)partial + 1 < (1 << 8)) {
      partial ++;
      return;
    }
    else {
      partial = 0;
    }
  }
}

inline Key operator+(const Key& a, uint8_t b) {
  Key res = a;
  for (int i = 0; i < (int)define::keyLen; ++ i) {
    auto& partial = res.at(define::keyLen - 1 - i);
    if ((int)partial + b < (1 << 8)) {
      partial += b;
      break;
    }
    else {
      auto tmp = ((int)partial + b);
      partial = tmp % (1 << 8);
      b = tmp / (1 << 8);
    }
  }
  return res;
}

inline Key operator-(const Key& a, uint8_t b) {
  Key res = a;
  for (int i = 0; i < (int)define::keyLen; ++ i) {
    auto& partial = res.at(define::keyLen - 1 - i);
    if (partial >= b) {
      partial -= b;
      break;
    }
    else {
      int carry = 0, tmp = partial;
      while(tmp < b) tmp += (1 << 8), carry ++;
      partial = ((int)partial + carry * (1 << 8)) - b;
      b = carry;
    }
  }
  return res;
}

inline uint64_t key2int(const Key& key) {
  uint64_t res = 0;
  // for (auto a : key) res = (res << 8) + a;
  std::string from_key ="";
  for(int i = 0; i < define::keyLen; i++) {
    from_key += key[i];
  }
  res = atoi(from_key.c_str());
  return res;
}

// TODO: change to uint64()
inline Key int2key(uint64_t key) {
#ifdef KEY_SPACE_LIMIT
  key = key % (define::kKeyMax - define::kKeyMin) + define::kKeyMin;
#else
  if (key == key2int(define::kkeyNull)) key = define::kKeyMin;
#endif
  Key res{};
  for (int i = 1; i <= (int)define::keyLen; ++ i) {
    auto shr = (define::keyLen - i) * 8;
    res.at(i - 1) = (shr >= 64u ? 0 : ((key >> shr) & ((1 << 8) - 1))); // Is equivalent to padding zero for short key
  }
  return res;
}

inline Key str2key(const std::string &key) {
  // assert(key.size() <= define::keyLen);
  Key res{};
  std::copy(key.begin(), key.size() <= define::keyLen ? key.end() : key.begin() + define::keyLen, res.begin());
  return res;
}

inline uint16_t key2fp(const Key& key) {
  uint16_t res = key.at(define::keyLen - 1);
  return (res << 8) + key.at(define::keyLen - 2);
}

inline std::ostream&
operator<<( std::ostream& dest, __uint128_t value )
{
    std::ostream::sentry s( dest );
    if ( s ) {
        __uint128_t tmp = value < 0 ? -value : value;
        char buffer[ 128 ];
        char* d = std::end( buffer );
        do
        {
            -- d;
            *d = "0123456789"[ tmp % 10 ];
            tmp /= 10;
        } while ( tmp != 0 );
        if ( value < 0 ) {
            -- d;
            *d = '-';
        }
        int len = std::end( buffer ) - d;
        if ( dest.rdbuf()->sputn( d, len ) != len ) {
            dest.setstate( std::ios_base::badbit );
        }
    }
    return dest;
}

// 问题所在，对应的key无法有效转换位int类型来进行f(x)的转换和计算
inline __uint128_t key2int128(const Key &key) {
  __uint128_t res = 0;
  // int len = std::min((int)key.size(), 16);
  int len = key.size();
  // 这里最高只支持16字节的key，因为最大是128位，我们这里不用，因为是每位最多是10个
  // for (int i = 0; i < len; ++ i) res = (res << 8) + key.at(i);
    std::string from_key ="";
    for(int i = 0; i < define::keyLen; i++) {
        from_key += key[i];
        res = (res * 10) + (int(key[i]) - 48);
    }
  // std::cout<<"insert "<<from_key<<" "<<__uint128_t(res)<<std::endl;
  return res;
}

#endif // _KEY_H_
