#ifndef CPPGCPTR_IALLOCATABLE_H
#define CPPGCPTR_IALLOCATABLE_H

#include <cstddef>

class IAllocatable {
public:
    IAllocatable() = default;

    virtual ~IAllocatable() = default;

    virtual void* allocate(size_t) = 0;

    virtual void free(void*, size_t) = 0;
};


#endif //CPPGCPTR_IALLOCATABLE_H
