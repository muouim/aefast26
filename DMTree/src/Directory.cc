#include "Directory.h"

Gaddr g_root_ptr = Gaddr::Null();
int g_root_level = -1;
bool enable_cache;

Directory::Directory(DirectoryConnection* dCon, ConnectionInfo* remoteInfo,
                     uint32_t machineNR, uint16_t dirID, uint16_t nodeID)
    : dCon(dCon)
    , remoteInfo(remoteInfo)
    , machineNR(machineNR)
    , dirID(dirID)
    , nodeID(nodeID)
    , dirTh(nullptr) {

	{ // chunck alloctor
		Gaddr dsm_start;
		uint64_t per_directory_dsm_size = dCon->dsmSize / NR_DIRECTORY;
		dsm_start.nodeID = nodeID;
		dsm_start.offset = per_directory_dsm_size * dirID;
		chunckAlloc = new GlobalAllocator(dsm_start, per_directory_dsm_size);
	}

	dirTh = new std::thread(&Directory::dirThread, this);
}

Directory::~Directory() { delete chunckAlloc; }

void Directory::dirThread() {

	bindCore(80 - dirID);
	Debug::notifyInfo("thread %d in memory nodes runs...\n", dirID);

	while(true) {
		struct ibv_wc wc;
		pollWithCQ(dCon->cq, 1, &wc);

		switch(int(wc.opcode)) {
		case IBV_WC_RECV: {
			auto* m = (RawMessage*)dCon->message->getMessage();
			process_message(m);

			break;
		}
		case IBV_WC_RDMA_WRITE: {
			break;
		}
		case IBV_WC_RECV_RDMA_WITH_IMM: {
			break;
		}
		default:
			assert(false);
		}
	}
}

void Directory::process_message(const RawMessage* m) {

	RawMessage* send = nullptr;
	switch(m->type) {
	case RpcType::MALLOC: {
		send = (RawMessage*)dCon->message->getSendPool();
		send->addr = chunckAlloc->alloc_chunck();
        std::cout<<"Now use memory: "<<send->addr.offset<<std::endl;

		break;
	}

	case RpcType::NEW_ROOT: {
		if(g_root_level < m->level) {
			g_root_ptr = m->addr;
			g_root_level = m->level;
			if(g_root_level >= 3) {
				enable_cache = true;
			}
		}

		break;
	}

	default:
		assert(false);
	}

	if(send) {
		dCon->sendMessage2App(send, m->node_id, m->app_id);
	}
}