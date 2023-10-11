#ifndef CPPGCPTR_GCMEMORYMANAGER_H
#define CPPGCPTR_GCMEMORYMANAGER_H

#include <iostream>
#include <vector>
#include <queue>
#include <stack>
#include <deque>
#include <map>
#include <mutex>
#include "IAllocatable.h"
#include "GCParameter.h"

class MemoryBlock {
private:
    void* address;
public:
    size_t size;

    MemoryBlock(void* start_address, size_t size_) : address(start_address), size(size_) {
    }

    MemoryBlock() = delete;

    void* getEndAddress() {
        return reinterpret_cast<void*>(reinterpret_cast<char*>(address) + size);
    }

    void* getStartAddress() {
        return address;
    }

    void shrink_from_head(size_t);

    void shrink_from_back(size_t);

    void grow_from_head(size_t);

    void grow_from_back(size_t);
};

class GCMemoryManager : public IAllocatable {
private:
    std::deque<MemoryBlock> freeList;
    std::map<void*, size_t> new_mem_map;
    std::recursive_mutex allocate_mutex_;
public:
    GCMemoryManager() = default;

    GCMemoryManager(const GCMemoryManager&) = delete;

    GCMemoryManager(GCMemoryManager&&) noexcept;

    void* allocate(size_t size) override;

    void free(void* address, size_t size) override;

    void add_memory(size_t size);

    void return_reserved();
};


#endif //CPPGCPTR_GCMEMORYMANAGER_H
