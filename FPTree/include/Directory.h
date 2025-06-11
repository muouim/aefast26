#ifndef __DIRECTORY_H__
#define __DIRECTORY_H__

#include "DMConnect.h"
#include "RdmaConfig.h"
#include <thread>
#include <unordered_map>

// memory server alloc memory
class GlobalAllocator {

public:
	GlobalAllocator(const Gaddr& start, size_t size)
	    : start(start)
	    , size(size) {
		bitmap_len = size / define::kChunkSize;
		bitmap = new bool[bitmap_len];
		memset(bitmap, 0, bitmap_len);

		// null ptr
		bitmap[0] = true;
		bitmap_tail = 1;
	}

	~GlobalAllocator() { delete[] bitmap; }
	Gaddr alloc_chunck() {
		Gaddr res = start;
		if(bitmap_tail >= bitmap_len) {
			assert(false);
			Debug::notifyError("shared memory space run out");
		}

		if(bitmap[bitmap_tail] == false) {
			bitmap[bitmap_tail] = true;
			res.offset += bitmap_tail * define::kChunkSize;

			bitmap_tail++;
		} else {
			assert(false);
			Debug::notifyError("TODO");
		}

		return res;
	}

	void free_chunk(const Gaddr& addr) {
		bitmap[(addr.offset - start.offset) / define::kChunkSize] = false;
	}

private:
	Gaddr start;
	size_t size;

	bool* bitmap;
	size_t bitmap_len;
	size_t bitmap_tail;
};

class Directory {
public:
	Directory(DirectoryConnection* dCon, ConnectionInfo* remoteInfo,
	          uint32_t machineNR, uint16_t dirID, uint16_t nodeID);

	~Directory();

private:
	DirectoryConnection* dCon;
	ConnectionInfo* remoteInfo;

	uint32_t machineNR;
	uint16_t dirID;
	uint16_t nodeID;

	std::thread* dirTh;

	GlobalAllocator* chunckAlloc;

	void dirThread();

	void sendData2App(const RawMessage* m);

	void process_message(const RawMessage* m);
};

#endif /* __DIRECTORY_H__ */
