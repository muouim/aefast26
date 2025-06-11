#include "DMConnect.h"

MessageConnection::MessageConnection(ibv_qp_type type, uint16_t sendPadding,
                                     uint16_t recvPadding, RdmaContext& ctx,
                                     ibv_cq* cq, uint32_t messageNR)
    : messageNR(messageNR)
    , curMessage(0)
    , curSend(0)
    , sendCounter(0)
    , sendPadding(sendPadding)
    , recvPadding(recvPadding) {

	assert(messageNR % kBatchCount == 0);

	send_cq = ibv_create_cq(ctx.ctx, 128, NULL, NULL, 0);

	createQueuePair(&message, type, send_cq, cq, &ctx);
	modifyUDtoRTS(message, &ctx);

	messagePool = hugePageAlloc(2 * messageNR * MESSAGE_SIZE);
	messageMR = createMemoryRegion((uint64_t)messagePool,
	                               2 * messageNR * MESSAGE_SIZE, &ctx);
	sendPool = (char*)messagePool + messageNR * MESSAGE_SIZE;
	messageLkey = messageMR->lkey;
}

void MessageConnection::initRecv() {
	subNR = messageNR / kBatchCount;

	for(int i = 0; i < kBatchCount; ++i) {
		recvs[i] = new ibv_recv_wr[subNR];
		recv_sgl[i] = new ibv_sge[subNR];
	}

	for(int k = 0; k < kBatchCount; ++k) {
		for(size_t i = 0; i < subNR; ++i) {
			auto& s = recv_sgl[k][i];
			memset(&s, 0, sizeof(s));

			s.addr = (uint64_t)messagePool + (k * subNR + i) * MESSAGE_SIZE;
			s.length = MESSAGE_SIZE;
			s.lkey = messageLkey;

			auto& r = recvs[k][i];
			memset(&r, 0, sizeof(r));

			r.sg_list = &s;
			r.num_sge = 1;
			r.next = (i == subNR - 1) ? NULL : &recvs[k][i + 1];
		}
	}

	struct ibv_recv_wr* bad;
	for(int i = 0; i < kBatchCount; ++i) {
		if(ibv_post_recv(message, &recvs[i][0], &bad)) {
			Debug::notifyError("Receive failed.");
		}
	}
}

char* MessageConnection::getMessage() {
	struct ibv_recv_wr* bad;
	char* m = (char*)messagePool + curMessage * MESSAGE_SIZE + recvPadding;

	ADD_ROUND(curMessage, messageNR);

	if(curMessage % subNR == 0) {
		if(ibv_post_recv(
		       message,
		       &recvs[(curMessage / subNR - 1 + kBatchCount) % kBatchCount][0],
		       &bad)) {
			Debug::notifyError("Receive failed.");
		}
	}

	return m;
}

char* MessageConnection::getSendPool() {
	char* s = (char*)sendPool + curSend * MESSAGE_SIZE + sendPadding;

	ADD_ROUND(curSend, messageNR);

	return s;
}

void MessageConnection::sendRawMessage(RawMessage* m, uint32_t remoteQPN,
                                       ibv_ah* ah) {

	if((sendCounter & SIGNAL_BATCH) == 0 && sendCounter > 0) {
		ibv_wc wc;
		pollWithCQ(send_cq, 1, &wc);
	}

	rdmaSend(message, (uint64_t)m - sendPadding,
	         sizeof(RawMessage) + sendPadding, messageLkey, ah, remoteQPN,
	         (sendCounter & SIGNAL_BATCH) == 0);

	++sendCounter;
}

DirectoryConnection::DirectoryConnection(uint16_t dirID, void* dsmPool,
                                         uint64_t dsmSize, uint32_t machineNR,
                                         ConnectionInfo* remoteInfo)
    : dirID(dirID)
    , remoteInfo(remoteInfo) {

	createContext(&ctx);
	cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);
	message = new MessageConnection(IBV_QPT_UD, 0, 40, ctx, cq, DIR_MESSAGE_NR);

	message->initRecv();
	// message->initSend();

	// dsm memory
	this->dsmPool = dsmPool;
	this->dsmSize = dsmSize;
	this->dsmMR = createMemoryRegion((uint64_t)dsmPool, dsmSize, &ctx);
	this->dsmLKey = dsmMR->lkey;

	// on-chip lock memory
	if(dirID == 0) {
		this->lockPool = (void*)define::kLockStartAddr;
		this->lockSize = define::kLockChipMemSize;
		this->lockMR = createMemoryRegionOnChip((uint64_t)this->lockPool,
		                                        this->lockSize, &ctx);
		this->lockLKey = lockMR->lkey;
	}

	// app, RC
	for(int i = 0; i < MAX_THREAD_NUM; ++i) {
		data2app[i] = new ibv_qp*[machineNR];
		for(size_t k = 0; k < machineNR; ++k) {
			createQueuePair(&data2app[i][k], IBV_QPT_RC, cq, &ctx);
		}
	}
}

void DirectoryConnection::sendMessage2App(RawMessage* m, uint16_t node_id,
                                          uint16_t th_id) {
	message->sendRawMessage(m, remoteInfo[node_id].appMessageQPN[th_id],
	                        remoteInfo[node_id].dirToAppAh[dirID][th_id]);
	;
}

ThreadConnection::ThreadConnection(uint16_t threadID, void* cachePool,
                                   uint64_t cacheSize, uint32_t machineNR,
                                   ConnectionInfo* remoteInfo)
    : threadID(threadID)
    , remoteInfo(remoteInfo) {
	createContext(&ctx);

	cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);
	// rpc_cq = cq;
	rpc_cq = ibv_create_cq(ctx.ctx, RAW_RECV_CQ_COUNT, NULL, NULL, 0);

	message = new MessageConnection(IBV_QPT_UD, 0, 40, ctx, rpc_cq, APP_MESSAGE_NR);

	this->cachePool = cachePool;
	cacheMR = createMemoryRegion((uint64_t)cachePool, cacheSize, &ctx);
	cacheLKey = cacheMR->lkey;

	// dir, RC
	for(int i = 0; i < NR_DIRECTORY; ++i) {
		data[i] = new ibv_qp*[machineNR];
		for(size_t k = 0; k < machineNR; ++k) {
			createQueuePair(&data[i][k], IBV_QPT_RC, cq, &ctx);
		}
	}
}

void ThreadConnection::sendMessage2Dir(RawMessage* m, uint16_t node_id,
                                       uint16_t dir_id) {

	message->sendRawMessage(m, remoteInfo[node_id].dirMessageQPN[dir_id],
	                        remoteInfo[node_id].appToDirAh[threadID][dir_id]);
}
