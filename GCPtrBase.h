#ifndef CPPGCPTR_GCPTRBASE_H
#define CPPGCPTR_GCPTRBASE_H

#include <memory>
#include "ObjectInfo.h"
#include "GCPhase.h"

constexpr int GCPTR_IDENTIFIER_HEAD = 0x1f1e33fc;
constexpr int GCPTR_IDENTIFIER_TAIL = 0x03e0e1cc;

class GCPtrBase {
private:
    const int identifier_head = GCPTR_IDENTIFIER_HEAD;

protected:
    volatile MarkState inlineMarkState;      // 类似zgc的染色指针，加快“读取”染色标记

public:
    GCPtrBase() {
        if (GCPhase::duringGC())
            inlineMarkState = GCPhase::getCurrentMarkState();
        else
            inlineMarkState = MarkState::REMAPPED;
    }

    virtual ~GCPtrBase() = default;

    virtual void* getVoidPtr() = 0;

    virtual ObjectInfo getObjectInfo() = 0;

    MarkState getInlineMarkState() const {
        return inlineMarkState;
    }

    void setInlineMarkState(MarkState markstate) {
        this->inlineMarkState = markstate;
    }
};


#endif //CPPGCPTR_GCPTRBASE_H