#ifndef __KEEPER__H__
#define __KEEPER__H__

#include "RdmaConfig.h"
#include "RdmaVerbs.h"
#include <assert.h>
#include <functional>
#include <infiniband/verbs.h>
#include <libmemcached/memcached.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>

class Keeper {
private:
	uint32_t maxServer;
	uint16_t curServer;
	uint16_t myNodeID;
	std::string myIP;
	uint16_t myPort;

	memcached_st* memc;

protected:
	bool connectMemcached();
	bool disconnectMemcached();
	void serverConnect();
	void serverEnter();
	virtual bool connectNode(uint16_t remoteID) = 0;

public:
	Keeper(uint32_t maxServer = 12);
	~Keeper();

	uint16_t getMyNodeID() const { return this->myNodeID; }
	uint16_t getServerNR() const { return this->maxServer; }
	uint16_t getMyPort() const { return this->myPort; }

	std::string getMyIP() const { return this->myIP; }

	void memSet(const char* key, uint32_t klen, const char* val, uint32_t vlen);
	char* memGet(const char* key, uint32_t klen, size_t* v_size = nullptr);
	uint64_t memFetchAndAdd(const char* key, uint32_t klen);
};

// function implemented in header must be inline
inline std::string trim(const std::string& s) {
	std::string res = s;
	if(!res.empty()) {
		res.erase(0, res.find_first_not_of(" "));
		res.erase(res.find_last_not_of(" ") + 1);
	}
	return res;
}

inline Keeper::Keeper(uint32_t maxServer)
    : maxServer(maxServer)
    , curServer(0)
    , memc(NULL) {}

inline Keeper::~Keeper() {
	//   listener.detach();

	disconnectMemcached();
}

inline bool Keeper::connectMemcached() {
	memcached_server_st* servers = NULL;
	memcached_return rc;

	std::ifstream conf("../memcached.conf");

	if(!conf) {
		fprintf(stderr, "can't open memcached.conf\n");
		return false;
	}

	std::string addr, port;
	std::getline(conf, addr);
	std::getline(conf, port);

	memc = memcached_create(NULL);
	servers = memcached_server_list_append(servers, trim(addr).c_str(),
	                                       std::stoi(trim(port)), &rc);
	rc = memcached_server_push(memc, servers);

	if(rc != MEMCACHED_SUCCESS) {
		fprintf(stderr, "Counld't add server:%s\n",
		        memcached_strerror(memc, rc));
		sleep(1);
		return false;
	}

	memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
	return true;
}

inline bool Keeper::disconnectMemcached() {
	if(memc) {
		memcached_quit(memc);
		memcached_free(memc);
		memc = NULL;
	}
	return true;
}

inline void Keeper::serverEnter() {
	memcached_return rc;
	uint64_t serverNum;

	while(true) {
		rc = memcached_increment(memc, "serverNum", strlen("serverNum"), 1,
		                         &serverNum);
		if(rc == MEMCACHED_SUCCESS) {

			myNodeID = serverNum - 1;
			printf("I am servers %d\n", myNodeID);
			return;
		}
		fprintf(stderr,
		        "Server %d Counld't incr value and get ID: %s, retry...\n",
		        myNodeID, memcached_strerror(memc, rc));
		usleep(10000);
	}
}

inline void Keeper::serverConnect() {

	size_t l;
	uint32_t flags;
	memcached_return rc;

	while(curServer < maxServer) {
		char* serverNumStr = memcached_get(
		    memc, "serverNum", strlen("serverNum"), &l, &flags, &rc);
		if(rc != MEMCACHED_SUCCESS) {
			fprintf(stderr, "Server %d Counld't get serverNum: %s, retry\n",
			        myNodeID, memcached_strerror(memc, rc));
			continue;
		}
		uint32_t serverNum = atoi(serverNumStr);
		free(serverNumStr);

		// /connect server K
		for(size_t k = curServer; k < serverNum; ++k) {
			//if(k != myNodeID) {
			connectNode(k);
			printf("I connect server %zu\n", k);
			// }
		}
		curServer = serverNum;
	}
}

inline void Keeper::memSet(const char* key, uint32_t klen, const char* val,
                           uint32_t vlen) {

	memcached_return rc;
	while(true) {
		rc = memcached_set(memc, key, klen, val, vlen, (time_t)0, (uint32_t)0);
		if(rc == MEMCACHED_SUCCESS) {
			break;
		}
		usleep(400);
	}
}

inline char* Keeper::memGet(const char* key, uint32_t klen, size_t* v_size) {

	size_t l;
	char* res;
	uint32_t flags;
	memcached_return rc;

	while(true) {

		res = memcached_get(memc, key, klen, &l, &flags, &rc);
		if(rc == MEMCACHED_SUCCESS) {
			break;
		}
		usleep(400 * myNodeID);
	}

	if(v_size != nullptr) {
		*v_size = l;
	}

	return res;
}

inline uint64_t Keeper::memFetchAndAdd(const char* key, uint32_t klen) {
	uint64_t res;
	while(true) {
		memcached_return rc = memcached_increment(memc, key, klen, 1, &res);
		if(rc == MEMCACHED_SUCCESS) {
			return res;
		}
		usleep(10000);
	}
}

#endif
