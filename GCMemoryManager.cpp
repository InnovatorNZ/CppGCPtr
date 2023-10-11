#include "GCMemoryManager.h"

GCMemoryManager::GCMemoryManager(GCMemoryManager&& other) noexcept {
    std::clog << "GCMemoryManager(GCMemoryManager&&)" << std::endl;
    std::unique_lock<std::recursive_mutex> lock(other.allocate_mutex_);
    this->freeList = std::move(other.freeList);
    this->new_mem_map = std::move(other.new_mem_map);
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
    std::unique_lock<std::recursive_mutex> lock(this->allocate_mutex_);
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
    std::unique_lock<std::recursive_mutex> lock(this->allocate_mutex_);
    char* end_address = reinterpret_cast<char*>(start_address) + size;
    if (freeList.empty()) {
        freeList.emplace_back(start_address, size);
        return;
    }
    MemoryBlock& start_block = *freeList.begin();
    if (end_address <= start_block.getStartAddress()) {
        if (end_address == start_block.getStartAddress()) {
            start_block.grow_from_head(size);
        } else {
            freeList.emplace_front(start_address, size);
        }
        return;
    }
    MemoryBlock& end_block = *--freeList.end();
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

    std::cerr << "Warning: Invalid memory address to free. Please check." << std::endl;
}

void GCMemoryManager::add_memory(size_t size) {
    size_t malloc_size = std::max(size, GCParameter::secondaryMallocSize);
    void* new_memory = malloc(malloc_size);
    if (new_memory == nullptr) {
        std::clog << "Warning: GCMemoryManager fails to allocate more memory from OS." << std::endl;
        return;
    }
    std::unique_lock<std::recursive_mutex> lock(this->allocate_mutex_);
    new_mem_map.emplace(new_memory, malloc_size);
    this->free(new_memory, malloc_size);
    std::clog << "Info: GCMemoryManager allocated " << malloc_size << " bytes from OS." << std::endl;
}

void GCMemoryManager::return_reserved() {
    std::unique_lock<std::recursive_mutex> lock(this->allocate_mutex_);
    for (auto block = freeList.begin(); block != freeList.end(); ++block) {
        // 大于等于当前块起始位置的
        auto lower = new_mem_map.lower_bound(block->getStartAddress());
        if (lower == new_mem_map.end()) continue;
        char* newMemStartAddr = reinterpret_cast<char*>(lower->first);
        size_t newMemSize = lower->second;
        char* newMemEndAddr = newMemStartAddr + newMemSize;
        if (newMemEndAddr <= block->getEndAddress()) {
            const size_t firstHalfSize = newMemStartAddr - (char*) block->getStartAddress();
            const size_t secondHalfSize = (char*) block->getEndAddress() - newMemEndAddr;
            if (firstHalfSize > 0 && secondHalfSize > 0) {
                block->size = firstHalfSize;
                freeList.emplace(block + 1, newMemEndAddr, secondHalfSize);
            } else if (firstHalfSize > 0) {
                block->size = firstHalfSize;
            } else if (secondHalfSize > 0) {
                block->shrink_from_head(block->size - secondHalfSize);
            }
            new_mem_map.erase(lower);
            ::free(newMemStartAddr);
            std::clog << "Info: GCMemoryManager returned " << newMemSize << " bytes to OS." << std::endl;
        }
    }
}