#ifndef CPPGCPTR_IMEMORYALLOCATOR_H
#define CPPGCPTR_IMEMORYALLOCATOR_H

#include <cstddef>
#include <memory>

class GCRegion;

class IMemoryAllocator {
public:
    IMemoryAllocator() = default;

    virtual ~IMemoryAllocator() = default;

    virtual std::pair<void*, std::shared_ptr<GCRegion>> allocate(size_t) = 0;

    virtual void* allocate_raw(size_t) = 0;

    virtual void free(void*, size_t) = 0;
};


#endif //CPPGCPTR_IMEMORYALLOCATOR_H