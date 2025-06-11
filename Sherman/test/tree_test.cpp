#include "DSM.h"
#include "Tree.h"

int main() {

  DSMConfig config;
  // config.machineNR = 2;
  config.machineNR = 1;
  DSM *dsm = DSM::getInstance(config);
 
  dsm->registerThread();

  auto tree = new Tree(dsm);

  Value v;

  if (dsm->getMyNodeID() != 0) {
    while(true);
  }


  for (uint64_t i = 1; i < 10240; ++i) {
    Key t_key;
    std::string s_key = std::to_string(i);
    s_key.resize(KEY_SIZE, '0');
    memcpy(t_key.key, s_key.c_str(), KEY_SIZE);
    tree->insert(t_key, i * 2);
    // tree->insert(i, i * 2);
  }

  for (uint64_t i = 10240 - 1; i >= 1; --i) {
    Key t_key;
    std::string s_key = std::to_string(i);
    s_key.resize(KEY_SIZE, '0');
    memcpy(t_key.key, s_key.c_str(), KEY_SIZE);
    tree->insert(t_key, i * 3);
    // tree->insert(i, i * 3);
  }

  for (uint64_t i = 1; i < 10240; ++i) {
    Key t_key;
    std::string s_key = std::to_string(i);
    s_key.resize(KEY_SIZE, '0');
    memcpy(t_key.key, s_key.c_str(), KEY_SIZE);
    tree->insert(t_key, i * 3);
    auto res = tree->search(t_key, v);
    assert(res && v == i * 3);
    // std::cout << "search result:  " << res << " v: " << v << std::endl;
    // auto res = tree->search(i, v);
    // assert(res && v == i * 3);
    // std::cout << "search result:  " << res << " v: " << v << std::endl;
  }

  printf("Hello\n");

  while (true)
    ;
}