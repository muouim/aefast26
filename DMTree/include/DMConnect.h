#ifndef __DM_CONNECTION_H__
#define __DM_CONNECTION_H__

#include "RdmaConfig.h"
#include "RdmaVerbs.h"

#define SIGNAL_BATCH 31

class Message;

struct ConnectionInfo {
	// directory
	uint64_t dsmBase;

	uint32_t dsmRKey[NR_DIRECTORY];
	uint32_t dirMessageQPN[NR_DIRECTORY];
	ibv_ah* appToDirAh[MAX_THREAD_NUM][NR_DIRECTORY];

	// cache
	uint64_t cacheBase;

	// lock memory
	uint64_t lockBase;
	uint32_t lockRKey[NR_DIRECTORY];

	// app thread
	uint32_t appRKey[MAX_THREAD_NUM];
	uint32_t appMessageQPN[MAX_THREAD_NUM];
	ibv_ah* dirToAppAh[NR_DIRECTORY][MAX_THREAD_NUM];
};

enum RpcType : uint8_t {
	MALLOC,
	FREE,
	NEW_ROOT,
	NOP,
};

struct RawMessage {
	RpcType type;

	uint16_t node_id;
	uint16_t app_id;

	Gaddr addr; // for malloc
	int level;
} __attribute__((packed));

// #messageNR send pool and #messageNR message pool
class MessageConnection {
	static const int kBatchCount = 4;

protected:
	ibv_qp* message; // ud or raw packet
	uint16_t messageNR;

	ibv_mr* messageMR;
	void* messagePool;
	uint32_t messageLkey;

	uint16_t curMessage;

	void* sendPool;
	uint16_t curSend;

	ibv_recv_wr* recvs[kBatchCount];
	ibv_sge* recv_sgl[kBatchCount];
	uint32_t subNR;

	ibv_cq* send_cq;
	uint64_t sendCounter;

	uint16_t sendPadding; // ud: 0
	                      // rp: ?
	uint16_t recvPadding; // ud: 40
	                      // rp: ?

public:
	MessageConnection(ibv_qp_type type, uint16_t sendPadding,
	                  uint16_t recvPadding, RdmaContext& ctx, ibv_cq* cq,
	                  uint32_t messageNR);
	~MessageConnection();

	void initRecv();

	char* getMessage();
	char* getSendPool();
	void sendRawMessage(RawMessage* m, uint32_t remoteQPN, ibv_ah* ah);

	uint32_t getQPN() { return message->qp_num; }
};

// directory thread - memory server
struct DirectoryConnection {
	uint16_t dirID;

	RdmaContext ctx;
	ibv_cq* cq;

	MessageConnection* message;

	ibv_qp** data2app[MAX_THREAD_NUM];

	ibv_mr* dsmMR;
	void* dsmPool;
	uint64_t dsmSize;
	uint32_t dsmLKey;

	ibv_mr* lockMR;
	void* lockPool; // address on-chip
	uint64_t lockSize;
	uint32_t lockLKey;

	ConnectionInfo* remoteInfo;

	DirectoryConnection(uint16_t dirID, void* dsmPool, uint64_t dsmSize,
	                    uint32_t machineNR, ConnectionInfo* remoteInfo);

	void sendMessage2App(RawMessage* m, uint16_t node_id, uint16_t th_id);
};

// app thread - compute server
struct ThreadConnection {

	uint16_t threadID;

	RdmaContext ctx;
	ibv_cq* cq; // for one-side verbs
	ibv_cq* rpc_cq;

	MessageConnection* message;

	ibv_qp** data[NR_DIRECTORY];

	ibv_mr* cacheMR;
	void* cachePool;
	uint32_t cacheLKey;
	ConnectionInfo* remoteInfo;

	ThreadConnection(uint16_t threadID, void* cachePool, uint64_t cacheSize,
	                 uint32_t machineNR, ConnectionInfo* remoteInfo);

	void sendMessage2Dir(RawMessage* m, uint16_t node_id, uint16_t dir_id = 0);
};

#endif /* __DIRECTORYCONNECTION_H__ */
