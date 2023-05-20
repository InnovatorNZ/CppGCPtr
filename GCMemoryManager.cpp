#include "GCMemoryManager.h"

GCMemoryManager::GCMemoryManager(void* memoryStart, size_t size) {
    freeList.emplace_back(memoryStart, size);
}

GCMemoryManager::GCMemoryManager(const GCMemoryManager& other) {
    this->freeList = other.freeList;
}

GCMemoryManager::GCMemoryManager(GCMemoryManager&& other) noexcept {
    std::unique_lock<std::mutex> lock(other.allocate_mutex_);
    std::clog << "GCMemoryManager(GCMemoryManager&&)" << std::endl;
    this->freeList = std::move(other.freeList);
}

void MemoryBlock::shrink_from_head(size_t _size) {
    if (_size >= size) {
        std::clog << "Size larger than memory block" << std::endl;
        return;
    }
    address = reinterpret_cast<void*>(reinterpret_cast<char*>(address) + _size);
    size -= _size;
}

void MemoryBlock::shrink_from_back(size_t _size) {
    if (_size >= size) {
        std::clog << "Size larger than memory block" << std::endl;
        return;
    }
    size -= _size;
}

void MemoryBlock::grow_from_head(size_t _size) {
    address = reinterpret_cast<void*>(reinterpret_cast<char*>(address) - _size);
    size += _size;
}

void MemoryBlock::grow_from_back(size_t _size) {
    size += _size;
}

void* GCMemoryManager::allocate(size_t size) {
    std::unique_lock<std::mutex> lock(this->allocate_mutex_);
    void* ret_addr = nullptr;
    for (auto it = freeList.begin(); it != freeList.end(); it++) {
        MemoryBlock& memoryBlock = *it;
        if (memoryBlock.size >= size) {
            ret_addr = memoryBlock.getStartAddress();
            if (memoryBlock.size > size)
                memoryBlock.shrink_from_head(size);
            else
                freeList.erase(it);
            break;
        }
    }
    return ret_addr;
}

void GCMemoryManager::free(void* start_address, size_t size) {
    std::unique_lock<std::mutex> lock(this->allocate_mutex_);
    char* end_address = reinterpret_cast<char*>(start_address) + size;
    MemoryBlock& start_block = *freeList.begin();
    MemoryBlock& end_block = *freeList.end();
    if (end_address <= start_block.getStartAddress()) {
        if (end_address == start_block.getStartAddress()) {
            start_block.grow_from_head(size);
        } else {
            freeList.emplace_front(start_address, size);
        }
        return;
    }
    if (start_address >= end_block.getEndAddress()) {
        if (start_address == end_block.getEndAddress()) {
            end_block.grow_from_back(size);
        } else {
            freeList.emplace_back(start_address, size);
        }
        return;
    }
    auto it_next = ++freeList.begin(), it_prev = freeList.begin();
    for (; it_next != freeList.end();) {
        MemoryBlock& next_block = *it_next;
        MemoryBlock& prev_block = *it_prev;
        if (start_address >= prev_block.getEndAddress() && end_address <= next_block.getStartAddress()) {
            if (start_address == prev_block.getEndAddress() && end_address == next_block.getStartAddress()) {
                next_block.grow_from_head(size + prev_block.size);
                freeList.erase(it_prev);
            } else if (start_address == prev_block.getEndAddress()) {
                prev_block.grow_from_back(size);
            } else if (end_address == next_block.getStartAddress()) {
                next_block.grow_from_head(size);
            } else {
                freeList.insert(it_next, MemoryBlock(start_address, size));
            }
            return;
        } else {
            it_next++;
            it_prev++;
        }
    }
    std::cerr << "Invalid memory address to free. Please check." << std::endl;
}