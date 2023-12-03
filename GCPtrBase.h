#ifndef CPPGCPTR_GCPTRBASE_H
#define CPPGCPTR_GCPTRBASE_H

#include <memory>
#include <atomic>
#include "ObjectInfo.h"
#include "GCPhase.h"
#include "GCParameter.h"

constexpr int GCPTR_IDENTIFIER_HEAD = 0x1f1e33fc;
constexpr int GCPTR_IDENTIFIER_TAIL = 0x03e0e1cc;

class GCPtrBase {
private:
    const int identifier_head = GCPTR_IDENTIFIER_HEAD;

protected:
    std::atomic<MarkState> inlineMarkState;

private:
    size_t rootset_offset = 0;

public:
    GCPtrBase() {
        if (GCPhase::duringGC())
            inlineMarkState = GCPhase::getCurrentMarkState();
        else
            inlineMarkState = MarkState::REMAPPED;
    }

    GCPtrBase(const GCPtrBase& other) {
        setInlineMarkState(other);
    }

    virtual ~GCPtrBase() {
        inlineMarkState = MarkState::DE_ALLOCATED;
    }

    virtual void* getVoidPtr() = 0;

    virtual ObjectInfo getObjectInfo() = 0;

    MarkState getInlineMarkState() const {
        return inlineMarkState;
    }

    void setInlineMarkState(MarkState markstate) {
        this->inlineMarkState = markstate;
    }

    void setInlineMarkState(const GCPtrBase& other) {
        if (GCPhase::duringMarking()) {
            if constexpr (GCParameter::useCopiedMarkstate)
                inlineMarkState = MarkState::COPIED;
            else
                inlineMarkState = GCPhase::getCurrentMarkState();
        } else {
            inlineMarkState.store(other.getInlineMarkState());
        }
    }

    bool casInlineMarkState(MarkState expected, MarkState target) {
        return inlineMarkState.compare_exchange_weak(expected, target);
    }

    void setRootsetOffset(int p) {
        this->rootset_offset = p;
    }
};


#endif //CPPGCPTR_GCPTRBASE_H