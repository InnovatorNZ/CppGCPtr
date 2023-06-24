#ifndef CPPGCPTR_OBJECTINFO_H
#define CPPGCPTR_OBJECTINFO_H

#include <cstddef>
#include "GCRegion.h"

struct ObjectInfo {
    void* object_addr;
    size_t object_size;
    GCRegion* region;
};


#endif //CPPGCPTR_OBJECTINFO_H