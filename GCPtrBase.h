#ifndef CPPGCPTR_GCPTRBASE_H
#define CPPGCPTR_GCPTRBASE_H

#include "GCPhase.h"

#define GCPTR_IDENTIFIER 0x1f1e33fc

class GCPtrBase {
private:
    const int identifier = GCPTR_IDENTIFIER;

protected:
    MarkState inlineMarkState;      // 有点类似zgc的染色指针，加快“读取”染色标记

public:
    GCPtrBase() : inlineMarkState(MarkState::REMAPPED) {     // 不管是不是处于GC阶段，都初始化为Remapped，让mark对此进行处理
    }

    virtual ~GCPtrBase() = default;

    virtual void* getVoidPtr() const = 0;

    virtual unsigned int getObjectSize() const = 0;

    MarkState getInlineMarkState() const {
        return inlineMarkState;
    }

    void setInlineMarkState(MarkState markstate) {
        this->inlineMarkState = markstate;
    }
};


#endif //CPPGCPTR_GCPTRBASE_H