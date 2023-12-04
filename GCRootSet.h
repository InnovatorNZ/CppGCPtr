#ifndef CPPGCPTR_GCROOTSET_H
#define CPPGCPTR_GCROOTSET_H

#include <vector>
#include <list>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include "GCPtrBase.h"
#include "Iterator.h"

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
        from->setRootsetOffset(p_tail);
        p_tail++;
    }

    void remove(GCPtrBase* from) {
        size_t p = from->getRootsetOffset();
        if (p >= p_tail || p == 0)
            throw std::invalid_argument("GCRootSet::remove(): p is greater than p_tail or is equal to 0");
        int c_idx = p / SINGLE_BLOCK_SIZE;
        int c_offset = p % SINGLE_BLOCK_SIZE;
        if (address_arr[c_idx][c_offset] != from)
            throw std::logic_error("GCRootSet::remove(): p in GCPtr is not equal to p in root set");
        int p_tail_ = p_tail - 1;
        int tail_idx = p_tail_ / SINGLE_BLOCK_SIZE;
        int tail_offset = p_tail_ % SINGLE_BLOCK_SIZE;
        GCPtrBase* c_tail = address_arr[tail_idx][tail_offset];
        address_arr[c_idx][c_offset] = c_tail;
        c_tail->setRootsetOffset(p);
        p_tail--;
    }

    std::unique_ptr<Iterator<GCPtrBase*>> getIterator() {
        return std::make_unique<RootSetIterator>(*this);
    }

    class RootSetIterator : public Iterator<GCPtrBase*> {
    private:
        GCRootSet& rootSet;
        size_t p;

    public:
        explicit RootSetIterator(GCRootSet& rootSet) : rootSet(rootSet), p(0) {
        }

        GCPtrBase* current() const override {
            if (p == 0) return nullptr;
            int c_idx = p / GCRootSet::SINGLE_BLOCK_SIZE;
            int c_offset = p % GCRootSet::SINGLE_BLOCK_SIZE;
            return rootSet.address_arr[c_idx][c_offset];
        }

        bool MoveNext() override {
            p++;
            return p < rootSet.p_tail;
        }
    };

};


#endif //CPPGCPTR_GCROOTSET_H
