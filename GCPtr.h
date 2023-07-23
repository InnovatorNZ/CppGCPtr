#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include <atomic>
#include "GCPtrBase.h"
#include "GCWorker.h"

static GCWorker gcWorker_(enableConcurrentGC, enableMemoryAllocator, enableDestructorSupport,
         useInlineMarkState, useSecondaryMemoryManager, enableRelocation,
         enableParallelGC, enableReclaim);

template<typename T>
class GCPtr : public GCPtrBase {
    template<typename U>
    friend class GCPtr;

private:
    std::atomic<T*> obj;
    unsigned int obj_size;
    bool is_root;
    std::shared_ptr<GCRegion> region;
    const int identifier_tail = GCPTR_IDENTIFIER_TAIL;

    bool needHeal() const {
        return this->obj != nullptr && gcWorker_.relocationEnabled()
               && GCPhase::needSelfHeal(getInlineMarkState());
    }

    void selfHeal() {
        auto healed = gcWorker_.getHealedPointer(obj, obj_size, region.get());
        if (healed.first != nullptr) {
            this->obj = static_cast<T*>(healed.first);
            this->region = healed.second;
        }
        this->setInlineMarkState(MarkState::REMAPPED);
    }

public:
    GCPtr() : obj(nullptr), obj_size(0) {
        is_root = gcWorker_.is_root(this);
        if (is_root) {
            gcWorker_.addRoot(this);
        }
    }

    explicit GCPtr(T* obj, const std::shared_ptr<GCRegion>& region = nullptr, bool is_root = false) :
            obj_size(sizeof(*obj)) {
        GCPhase::EnterCriticalSection();
        this->obj = obj;
        this->region = region;
        if (!is_root && gcWorker_.is_root(this))
            is_root = true;
        this->is_root = is_root;
        if (gcWorker_.destructorEnabled())
            gcWorker_.registerDestructor(obj, [obj]() { obj->~T(); });
        if (is_root) {
            gcWorker_.addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    T* get() {
        if (this->needHeal())
            this->selfHeal();
        return this->obj;
    }

    T* get() const {
        // Calling const get() will disable pointer self-heal, which is not recommend
        if (this->needHeal())
            return static_cast<T*>(gcWorker_.getHealedPointer(obj, obj_size, region.get()).first);
        else
            return obj;
    }

    T* operator->() {
        return this->get();
    }

    T* operator->() const {
        return this->get();
    }

    void* getVoidPtr() override {
        return reinterpret_cast<void*>(this->get());
    }

    ObjectInfo getObjectInfo() override {
        void* obj_addr = this->getVoidPtr();
        return ObjectInfo{obj_addr, obj_size, region.get()};
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            GCPhase::EnterCriticalSection();
            if (this->obj != nullptr && this->obj != other.obj && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                gcWorker_.addSATB(this->getObjectInfo());
            }
            this->setInlineMarkState(other.getInlineMarkState());
            this->obj.store(other.obj.load());
            this->obj_size = other.obj_size;
            this->region = other.region;
            /*
             * 赋值运算符重载无需再次判别is_root，有且仅有构造函数需要
            if (gcWorker_.is_root(this)) {
                this->is_root = true;
                gcWorker_.addRoot(this);
            }
            */
            GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    GCPtr& operator=(std::nullptr_t) {
        if (this->obj != nullptr) {
            if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCPhase::EnterCriticalSection();
                gcWorker_.addSATB(this->getObjectInfo());
                GCPhase::LeaveCriticalSection();
            }
            this->obj = nullptr;
            this->obj_size = 0;
            this->region = nullptr;
        }
        return *this;
    }

    bool operator==(GCPtr<T>& other) {
        return this->get() == other.get();
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr(const GCPtr& other) : obj_size(other.obj_size) {
        GCPhase::EnterCriticalSection();
        this->setInlineMarkState(other.getInlineMarkState());
        this->obj.store(other.obj.load());
        this->region = other.region;
        this->is_root = gcWorker_.is_root(this);
        if (is_root) {
            gcWorker_.addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    GCPtr(GCPtr&& other) noexcept : obj_size(other.obj_size) {
        GCPhase::EnterCriticalSection();
        this->setInlineMarkState(other.getInlineMarkState());
        this->obj.store(other.obj.load());
        this->region = std::move(other.region);
        this->is_root = gcWorker_.is_root(this);
        if (is_root) {
            gcWorker_.addRoot(this);
        }
        other.obj = nullptr;
        other.obj_size = 0;
        other.setInlineMarkState(MarkState::REMAPPED);
        GCPhase::LeaveCriticalSection();
    }

    template<typename U>
    GCPtr(GCPtr<U>&& other) noexcept : obj(other.obj), obj_size(other.obj_size) {
        this->setInlineMarkState(other.getInlineMarkState());
        this->region = std::move(other.region);
        this->is_root = gcWorker_.is_root(this);
        if (is_root) {
            gcWorker_.addRoot(this);
        }
        other.obj = nullptr;
        other.obj_size = 0;
        other.setInlineMarkState(MarkState::REMAPPED);
    }

    ~GCPtr() override {
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCPhase::EnterCriticalSection();
            gcWorker_.addSATB(this->getObjectInfo());
            GCPhase::LeaveCriticalSection();
        }
        if (is_root) {
            gcWorker_.removeRoot(this);
        }
    }
};

namespace gc {
    template<class T, class... Args>
    GCPtr <T> make_gc(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (gcWorker_.memoryAllocatorEnabled()) {
            auto pair = gcWorker_.allocate(sizeof(T));
            obj = static_cast<T*>(pair.first);
            region = pair.second;
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        return GCPtr<T>(obj, region);
    }

    template<class T, class... Args>
    GCPtr <T> make_static(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (gcWorker_.memoryAllocatorEnabled()) {
            auto pair = gcWorker_.allocate(sizeof(T));
            obj = static_cast<T*>(pair.first);
            region = pair.second;
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        return GCPtr<T>(obj, region, true);
    }

    void triggerGC() {
        gcWorker_.triggerGC();
    }

#ifdef OLD_MAKEGC
    template<typename T>
    GCPtr <T> make_gc(T* obj) {
        GCPtr<T> ret(obj);
        return ret;
    }

    template<typename T>
    GCPtr <T> make_root(T* obj) {
        GCPtr<T> ret(obj, true);
        return ret;
    }

    template<typename T>
    GCPtr <T> make_gc() {
        GCPhase::EnterCriticalSection();
        T* obj = new T();
        GCPhase::LeaveCriticalSection();
        GCPtr<T> ret(obj);
        return ret;
    }

    template<typename T>
    GCPtr <T> make_root() {
        GCPhase::EnterCriticalSection();
        T* obj = new T();
        GCPhase::LeaveCriticalSection();
        GCPtr<T> ret(obj, true);
        return ret;
    }
#endif

#ifdef MORE_USERFRIENDLY
    template<typename T>
    GCPtr<T> make_static() {
        return make_root<T>();
    }

    template<typename T>
    GCPtr<T> make_local() {
        return make_root<T>();
    }

    template<typename T>
    GCPtr<T> make_const() {
        const T* t = new T();
        return GCPtr<T>(t);
    }
#endif
}

#endif //CPPGCPTR_GCPTR_H
