#pragma once

#define _COMPILE_UNITTEST 0

#if _COMPILE_UNITTEST

#include "GCMemoryAllocator.h"

class MemoryAllocatorTest {
private:
    GCMemoryAllocator memoryAllocator;
public:
    void test() {
        using namespace std;
        const int size = 22000;
        GCPhase::SwitchToNextPhase();
        void* last = nullptr;
        for (int i = 0; i < 100000; i++) {
            void* object1 = memoryAllocator.allocate(size);
            if (last != nullptr) {
                std::shared_ptr<GCRegion> region = memoryAllocator.getRegion(last);
                //region->mark(object1, size);
                cout << MarkStateUtil::toString(region->bitmap->getMarkState(last)) << endl;
            }
            last = object1;
        }
        GCPhase::SwitchToNextPhase();
        GCPhase::SwitchToNextPhase();
        memoryAllocator.triggerRelocation();
        for (int i = 0; i < 1000; i++) {
            void* object1 = memoryAllocator.allocate(size);
            if (last != nullptr) {
                std::shared_ptr<GCRegion> region = memoryAllocator.getRegion(last);
                //region->mark(object1, size);
                cout << MarkStateUtil::toString(region->bitmap->getMarkState(last)) << endl;
            }
            last = object1;
        }
    }
};

#endif