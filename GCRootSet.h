#ifndef CPPGCPTR_GCROOTSET_H
#define CPPGCPTR_GCROOTSET_H

#include <vector>
#include <list>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include "GCPtrBase.h"

class GCRootSet {
private:
    std::vector<GCPtrBase**> address_arr;
    static constexpr int SINGLE_BLOCK_SIZE = 1024;
    size_t p_tail;

    void addBlock() {
        GCPtrBase** new_block = new GCPtrBase* [SINGLE_BLOCK_SIZE];
        address_arr.push_back(new_block);
    }

public:
    GCRootSet() : p_tail(1) {
    }

    void add(GCPtrBase* from) {
        int block_idx = p_tail / SINGLE_BLOCK_SIZE;
        int block_offset = p_tail % SINGLE_BLOCK_SIZE;
        if (block_idx >= address_arr.size()) {
            addBlock();
        }
        address_arr[block_idx][block_offset] = from;
        p_tail++;
    }

    void remove(int p) {
        if (p >= p_tail || p == 0)
            throw std::invalid_argument("GCRootSet::remove(): p is greater than p_tail or is equal to 0");
        int block_idx = p / SINGLE_BLOCK_SIZE;
        int block_offset = p % SINGLE_BLOCK_SIZE;
        int tail_idx = p / SINGLE_BLOCK_SIZE;
        int tail_offset = p % SINGLE_BLOCK_SIZE;
        GCPtrBase* c_tail = address_arr[tail_idx][tail_offset];
        address_arr[block_idx][block_offset] = c_tail;
        c_tail->setRootsetOffset(p);
        p_tail--;
    }

};


#endif //CPPGCPTR_GCROOTSET_H
