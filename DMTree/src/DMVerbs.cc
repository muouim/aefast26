#include "DMVerbs.h"

thread_local int DMVerbs::thread_id = -1;
thread_local ThreadConnection* DMVerbs::iCon = nullptr;
thread_local char* DMVerbs::rdma_buffer = nullptr;
thread_local RdmaBuffer DMVerbs::rbuf[define::kMaxCoro];
thread_local uint64_t DMVerbs::thread_tag = 0;
uint64_t rdma_cnt[MAX_THREAD_NUM][MAX_MACHINE_NUM];
uint64_t rdma_bw[MAX_THREAD_NUM][MAX_MACHINE_NUM];

DMVerbs* DMVerbs::getInstance(const DMConfig& conf) {
	static DMVerbs* dmv = nullptr;
	static WRLock lock;

	lock.wLock();
	if(!dmv) {
		dmv = new DMVerbs(conf);
	} else {
	}
	lock.wUnlock();

	return dmv;
}

DMVerbs::DMVerbs(const DMConfig& conf)
    : conf(conf)
    , appID(0)
    , cache(conf.cacheConfig) {

    use_memory_fp = false;
	baseAddr = (uint64_t)hugePageAlloc(conf.dsmSize * define::GB);

	Debug::notifyInfo("shared memory size: %dGB, 0x%lx", conf.dsmSize,
	                  baseAddr);
	Debug::notifyInfo("cache size: %dGB", conf.cacheConfig.cacheSize);

	// warmup
	// memset((char *)baseAddr, 0, conf.dsmSize * define::GB);
	for(uint64_t i = baseAddr; i < baseAddr + conf.dsmSize * define::GB;
	    i += 2 * define::MB) {
		*(char*)i = 0;
	}

	// clear up first chunk
	memset((char*)baseAddr, 0, define::kChunkSize);

	initRDMAConnection();

	Debug::notifyInfo("number of threads on memory node: %d", NR_DIRECTORY);
	for(int i = 0; i < NR_DIRECTORY; ++i) {
		dirAgent[i] =
		    new Directory(dirCon[i], remoteInfo, conf.machineNR, i, myNodeID);
	}

	keeper->barrier("DMVerbs-init");
}

void DMVerbs::registerThread() {

	static bool has_init[MAX_THREAD_NUM];

	if(thread_id != -1)
		return;

	thread_id = appID.fetch_add(1);
	thread_tag = thread_id + (((uint64_t)this->getMyNodeID()) << 32) + 1;
    bindCore(thread_id);

	iCon = thCon[thread_id];

	if(!has_init[thread_id]) {
		iCon->message->initRecv();
		// iCon->message->initSend();

		has_init[thread_id] = true;
	}

    rdma_buffer = (char *)cache.data + thread_id * define::kPerThreadRdmaBuf;

	for(int i = 0; i < define::kMaxCoro; ++i) {
		rbuf[i].set_buffer(rdma_buffer + i * define::kPerCoroRdmaBuf);
	}
}

void DMVerbs::initRDMAConnection() {

	Debug::notifyInfo("number of servers (colocated MN/CN): %d",
	                  conf.machineNR);

	remoteInfo = new ConnectionInfo[conf.machineNR];

	for(int i = 0; i < MAX_THREAD_NUM; ++i) {
		thCon[i] =
		    new ThreadConnection(i, (void*)cache.data, cache.size * define::GB,
		                         conf.machineNR, remoteInfo);
	}

	for(int i = 0; i < NR_DIRECTORY; ++i) {
		dirCon[i] = new DirectoryConnection(i, (void*)baseAddr,
		                                    conf.dsmSize * define::GB,
		                                    conf.machineNR, remoteInfo);
	}

	keeper = new RdmaKeeper(thCon, dirCon, remoteInfo, conf.machineNR);

	myNodeID = keeper->getMyNodeID();
    alloc_compute_nodes = myNodeID;
}

void DMVerbs::read(char* buffer, Gaddr gaddr, size_t size, bool signal,
                   CoroContext* ctx) {
    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
    rdma_bw[getMyThreadID()][gaddr.nodeID]+=size;
	if(ctx == nullptr) {
		rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		         remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
		         iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], signal);
	} else {
		rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		         remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
		         iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], true,
		         ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

void DMVerbs::read_sync(char* buffer, Gaddr gaddr, size_t size,
                        CoroContext* ctx) {
	read(buffer, gaddr, size, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::write(const char* buffer, Gaddr gaddr, size_t size, bool signal,
                    CoroContext* ctx) {

    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
    rdma_bw[getMyThreadID()][gaddr.nodeID]+=size;
	if(ctx == nullptr) {
		rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		          remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
		          iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], -1,
		          signal);
	} else {
		rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		          remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
		          iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], -1,
		          true, ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

void DMVerbs::write_sync(const char* buffer, Gaddr gaddr, size_t size,
                         CoroContext* ctx) {
	write(buffer, gaddr, size, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::fill_keys_dest(RdmaOpRegion& ror, Gaddr gaddr, bool is_chip) {
	ror.lkey = iCon->cacheLKey;
	if(is_chip) {
		ror.dest = remoteInfo[gaddr.nodeID].lockBase + gaddr.offset;
		ror.remoteRKey = remoteInfo[gaddr.nodeID].lockRKey[0];
	} else {
		ror.dest = remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset;
		ror.remoteRKey = remoteInfo[gaddr.nodeID].dsmRKey[0];
	}
}

void DMVerbs::read_batch(RdmaOpRegion* rs, int k, bool signal,
                         CoroContext* ctx) {

	int node_id = -1;
	for(int i = 0; i < k; ++i) {

		Gaddr gaddr;
		gaddr.val = rs[i].dest;
		node_id = gaddr.nodeID;
        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
        rdma_bw[getMyThreadID()][gaddr.nodeID]+=rs[i].size;
		fill_keys_dest(rs[i], gaddr, rs[i].is_on_chip);
	}

	if(ctx == nullptr) {
		rdmaReadBatch(iCon->data[0][node_id], rs, k, signal);
	} else {
		rdmaReadBatch(iCon->data[0][node_id], rs, k, true, ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

void DMVerbs::read_batch_sync(RdmaOpRegion* rs, int k, CoroContext* ctx) {
	read_batch(rs, k, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::write_batch(RdmaOpRegion* rs, int k, bool signal,
                          CoroContext* ctx) {

	int node_id = -1;
	for(int i = 0; i < k; ++i) {

		Gaddr gaddr;
		gaddr.val = rs[i].dest;
		node_id = gaddr.nodeID;
        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
        rdma_bw[getMyThreadID()][gaddr.nodeID]+=rs[i].size;
		fill_keys_dest(rs[i], gaddr, rs[i].is_on_chip);
	}

	if(ctx == nullptr) {
		rdmaWriteBatch(iCon->data[0][node_id], rs, k, signal);
	} else {
		rdmaWriteBatch(iCon->data[0][node_id], rs, k, true, ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

void DMVerbs::write_batch_sync(RdmaOpRegion* rs, int k, CoroContext* ctx) {
	write_batch(rs, k, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::write_faa(RdmaOpRegion& write_ror, RdmaOpRegion& faa_ror,
                        uint64_t add_val, bool signal, CoroContext* ctx) {
	int node_id;
	{
		Gaddr gaddr;
		gaddr.val = write_ror.dest;
		node_id = gaddr.nodeID;

        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
		fill_keys_dest(write_ror, gaddr, write_ror.is_on_chip);
	}
	{
		Gaddr gaddr;
		gaddr.val = faa_ror.dest;

        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
		fill_keys_dest(faa_ror, gaddr, faa_ror.is_on_chip);
	}
	if(ctx == nullptr) {
		rdmaWriteFaa(iCon->data[0][node_id], write_ror, faa_ror, add_val,
		             signal);
	} else {
		rdmaWriteFaa(iCon->data[0][node_id], write_ror, faa_ror, add_val, true,
		             ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}
void DMVerbs::write_faa_sync(RdmaOpRegion& write_ror, RdmaOpRegion& faa_ror,
                             uint64_t add_val, CoroContext* ctx) {
	write_faa(write_ror, faa_ror, add_val, true, ctx);
	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::write_cas(RdmaOpRegion& write_ror, RdmaOpRegion& cas_ror,
                        uint64_t equal, uint64_t val, bool signal,
                        CoroContext* ctx) {
	int node_id;
	{
		Gaddr gaddr;
		gaddr.val = write_ror.dest;
		node_id = gaddr.nodeID;
        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;

		fill_keys_dest(write_ror, gaddr, write_ror.is_on_chip);
	}
	{
		Gaddr gaddr;
		gaddr.val = cas_ror.dest;

        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
		fill_keys_dest(cas_ror, gaddr, cas_ror.is_on_chip);
	}
	if(ctx == nullptr) {
		rdmaWriteCas(iCon->data[0][node_id], write_ror, cas_ror, equal, val,
		             signal);
	} else {
		rdmaWriteCas(iCon->data[0][node_id], write_ror, cas_ror, equal, val,
		             true, ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}
void DMVerbs::write_cas_sync(RdmaOpRegion& write_ror, RdmaOpRegion& cas_ror,
                             uint64_t equal, uint64_t val, CoroContext* ctx) {
	write_cas(write_ror, cas_ror, equal, val, true, ctx);
	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::cas_read(RdmaOpRegion& cas_ror, RdmaOpRegion& read_ror,
                       uint64_t equal, uint64_t val, bool signal,
                       CoroContext* ctx) {

	int node_id;
	{
		Gaddr gaddr;
		gaddr.val = cas_ror.dest;
		node_id = gaddr.nodeID;
        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
		fill_keys_dest(cas_ror, gaddr, cas_ror.is_on_chip);
	}
	{
		Gaddr gaddr;
		gaddr.val = read_ror.dest;
        rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
		fill_keys_dest(read_ror, gaddr, read_ror.is_on_chip);
	}

	if(ctx == nullptr) {
		rdmaCasRead(iCon->data[0][node_id], cas_ror, read_ror, equal, val,
		            signal);
	} else {
		rdmaCasRead(iCon->data[0][node_id], cas_ror, read_ror, equal, val, true,
		            ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

bool DMVerbs::cas_read_sync(RdmaOpRegion& cas_ror, RdmaOpRegion& read_ror,
                            uint64_t equal, uint64_t val, CoroContext* ctx) {
	cas_read(cas_ror, read_ror, equal, val, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}

	return equal == *(uint64_t*)cas_ror.source;
}

void DMVerbs::cas(Gaddr gaddr, uint64_t equal, uint64_t val,
                  uint64_t* rdma_buffer, bool signal, CoroContext* ctx) {

    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
	if(ctx == nullptr) {
		rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
		                   remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset,
		                   equal, val, iCon->cacheLKey,
		                   remoteInfo[gaddr.nodeID].dsmRKey[0], signal);
	} else {
		rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
		                   remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset,
		                   equal, val, iCon->cacheLKey,
		                   remoteInfo[gaddr.nodeID].dsmRKey[0], true,
		                   ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

bool DMVerbs::cas_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
                       uint64_t* rdma_buffer, CoroContext* ctx) {
	cas(gaddr, equal, val, rdma_buffer, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}

	return equal == *rdma_buffer;
}

void DMVerbs::cas_mask(Gaddr gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, uint64_t mask, bool signal, CoroContext *ctx) {
  rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
  if (ctx == nullptr) {
    rdmaCompareAndSwapMask(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                          remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                          val, iCon->cacheLKey,
                          remoteInfo[gaddr.nodeID].dsmRKey[0], mask, signal);
  }
  else {
    rdmaCompareAndSwapMask(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                          remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                          val, iCon->cacheLKey,
                          remoteInfo[gaddr.nodeID].dsmRKey[0], mask, true, ctx->coro_id);
    (*ctx->yield)(*ctx->master);
  }
}

bool DMVerbs::cas_mask_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
                        uint64_t *rdma_buffer, uint64_t mask, CoroContext *ctx) {
  cas_mask(gaddr, equal, val, rdma_buffer, mask, true, ctx);

  if (ctx == nullptr) {
    ibv_wc wc;
    pollWithCQ(iCon->cq, 1, &wc);
  }

  return (equal & mask) == (*rdma_buffer & mask);
}

void DMVerbs::read_dm(char* buffer, Gaddr gaddr, size_t size, bool signal,
                      CoroContext* ctx) {

    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
	if(ctx == nullptr) {
		rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		         remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
		         iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], signal);
	} else {
		rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		         remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
		         iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], true,
		         ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

void DMVerbs::read_dm_sync(char* buffer, Gaddr gaddr, size_t size,
                           CoroContext* ctx) {
	read_dm(buffer, gaddr, size, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::write_dm(const char* buffer, Gaddr gaddr, size_t size,
                       bool signal, CoroContext* ctx) {
    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
	if(ctx == nullptr) {
		rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		          remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
		          iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], -1,
		          signal);
	} else {
		rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
		          remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
		          iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], -1,
		          true, ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

void DMVerbs::write_dm_sync(const char* buffer, Gaddr gaddr, size_t size,
                            CoroContext* ctx) {
	write_dm(buffer, gaddr, size, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}
}

void DMVerbs::cas_dm(Gaddr gaddr, uint64_t equal, uint64_t val,
                     uint64_t* rdma_buffer, bool signal, CoroContext* ctx) {

    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
	if(ctx == nullptr) {
		rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
		                   remoteInfo[gaddr.nodeID].lockBase + gaddr.offset,
		                   equal, val, iCon->cacheLKey,
		                   remoteInfo[gaddr.nodeID].lockRKey[0], signal);
	} else {
		rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
		                   remoteInfo[gaddr.nodeID].lockBase + gaddr.offset,
		                   equal, val, iCon->cacheLKey,
		                   remoteInfo[gaddr.nodeID].lockRKey[0], true,
		                   ctx->coro_id);
		(*ctx->yield)(*ctx->master);
	}
}

bool DMVerbs::cas_dm_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
                          uint64_t* rdma_buffer, CoroContext* ctx) {
	cas_dm(gaddr, equal, val, rdma_buffer, true, ctx);

	if(ctx == nullptr) {
		ibv_wc wc;
		pollWithCQ(iCon->cq, 1, &wc);
	}

	return equal == *rdma_buffer;
}

void DMVerbs::cas_dm_mask(Gaddr gaddr, uint64_t equal, uint64_t val,
                          uint64_t* rdma_buffer, uint64_t mask, bool signal) {
    rdma_cnt[getMyThreadID()][gaddr.nodeID]++;
	rdmaCompareAndSwapMask(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
	                       remoteInfo[gaddr.nodeID].lockBase + gaddr.offset,
	                       equal, val, iCon->cacheLKey,
	                       remoteInfo[gaddr.nodeID].lockRKey[0], mask, signal);
}

bool DMVerbs::cas_dm_mask_sync(Gaddr gaddr, uint64_t equal, uint64_t val,
                               uint64_t* rdma_buffer, uint64_t mask) {
	cas_dm_mask(gaddr, equal, val, rdma_buffer, mask);
	ibv_wc wc;
	pollWithCQ(iCon->cq, 1, &wc);

	return (equal & mask) == (*rdma_buffer & mask);
}

uint64_t DMVerbs::poll_rdma_cq(int count) {
	ibv_wc wc;
	pollWithCQ(iCon->cq, count, &wc);

	return wc.wr_id;
}

bool DMVerbs::poll_rdma_cq_once(uint64_t& wr_id) {
	ibv_wc wc;
	int res = pollOnce(iCon->cq, 1, &wc);

	wr_id = wc.wr_id;

	return res == 1;
}

int DMVerbs::poll_rdma_cqs(ibv_wc* wc) {
	int res = pollOnce(iCon->cq, 16, wc);

	return res;
}