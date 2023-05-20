#ifndef CPPGCPTR_GCMEMORYMANAGER_H
#define CPPGCPTR_GCMEMORYMANAGER_H

#include <iostream>
#include <vector>
#include <queue>
#include <stack>
#include <deque>
#include <mutex>

class MemoryBlock {
private:
    void* address;
public:
    size_t size;

    MemoryBlock(void* start_address, size_t size_) : address(start_address), size(size_) {
    }

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

class GCMemoryManager {
private:
    std::deque<MemoryBlock> freeList;
    std::mutex allocate_mutex_;
public:
    GCMemoryManager() = delete;

    GCMemoryManager(const GCMemoryManager&);

    GCMemoryManager(GCMemoryManager&&) noexcept;

    GCMemoryManager(void* memoryStart, size_t size);

    void* allocate(size_t size);

    void free(void* address, size_t size);
};


#endif //CPPGCPTR_GCMEMORYMANAGER_H
