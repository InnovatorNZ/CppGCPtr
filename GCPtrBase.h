#ifndef CPPGCPTR_GCPTRBASE_H
#define CPPGCPTR_GCPTRBASE_H

#include "GCPhase.h"

constexpr int GCPTR_IDENTIFIER_HEAD = 0x1f1e33fc;
constexpr int GCPTR_IDENTIFIER_TAIL = 0x03e0e1cc;

class GCPtrBase {
private:
    const int identifier_head = GCPTR_IDENTIFIER_HEAD;

protected:
    volatile MarkState inlineMarkState;      // 类似zgc的染色指针，加快“读取”染色标记

public:
    GCPtrBase() : inlineMarkState(MarkState::REMAPPED) {     // 不管是否处于GC阶段，都初始化为Remapped，让mark对此进行处理
    }

    virtual ~GCPtrBase() = default;

    virtual void* getVoidPtr() = 0;

    virtual unsigned int getObjectSize() const = 0;

    MarkState getInlineMarkState() const {
        return inlineMarkState;
    }

    void setInlineMarkState(MarkState markstate) {
        this->inlineMarkState = markstate;
    }
};


#endif //CPPGCPTR_GCPTRBASE_H