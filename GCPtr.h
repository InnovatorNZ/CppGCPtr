#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include <atomic>
#include "GCPtrBase.h"
#include "PtrGuard.h"
#include "GCWorker.h"

#define ENABLE_FREE_RESERVED 0

template<typename T>
class GCPtr_ : public GCPtrBase {
    template<typename U>
    friend
    class GCPtr_;

protected:
    T* obj;
    unsigned int obj_size;
    bool is_root;
    std::shared_ptr<GCRegion> region;
    std::unique_ptr<IReadWriteLock> ptrLock;
    const int identifier_tail = GCPTR_IDENTIFIER_TAIL;

    bool needHeal() const {
        return this->obj != nullptr && GCWorker::getWorker()->relocationEnabled()
               && GCPhase::needSelfHeal(getInlineMarkState());
    }

    bool needHeal(const MarkState& markState) const {
        return this->obj != nullptr && GCWorker::getWorker()->relocationEnabled()
               && GCPhase::needSelfHeal(markState);
    }

    void selfHeal(const MarkState& _markState) {
        if (_markState == MarkState::REMAPPED) return;
        auto healed = GCWorker::getWorker()->getHealedPointer(obj, obj_size, region.get());
        if (healed.first != nullptr) {
            this->obj = static_cast<T*>(healed.first);
            this->region = healed.second;
        }
        if (_markState == MarkState::COPIED && GCPhase::duringGC())
            this->casInlineMarkState(_markState, GCPhase::getCurrentMarkState());
        else
            this->casInlineMarkState(_markState, MarkState::REMAPPED);
    }

    void initPtrLock() {
        if constexpr (GCParameter::enablePtrRWLock)
            ptrLock = std::make_unique<WeakSpinReadWriteLock>();
        else
            ptrLock = nullptr;
    }

public:
    GCPtr_() : obj(nullptr), obj_size(0) {
        initPtrLock();
        is_root = GCWorker::getWorker()->is_root(this);
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        } else {
            GCWorker::getWorker()->addGCPtr(this);
        }
    }

    GCPtr_(std::nullptr_t) : GCPtr_() {
    }

    explicit GCPtr_(bool is_root) : obj(nullptr), obj_size(0), is_root(is_root) {
        initPtrLock();
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        } else {
            GCWorker::getWorker()->addGCPtr(this);
        }
    }

#if 0
    explicit GCPtr_(T* obj, const std::shared_ptr<GCRegion>& region = nullptr, bool is_root = false) {
        initPtrLock();
        GCPhase::EnterCriticalSection();
        if (!is_root && GCWorker::getWorker()->is_root(this))
            is_root = true;
        this->is_root = is_root;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        this->set(obj, region);
        GCPhase::LeaveCriticalSection();
    }
#endif

    T* getRaw() {
        MarkState mark_state = getInlineMarkState();
        if (this->needHeal(mark_state))
            this->selfHeal(mark_state);
        return this->obj;
    }

    T* getRaw() const {
        if (this->needHeal()) {
            void* healed_ptr = GCWorker::getWorker()->getHealedPointer(obj, obj_size, region.get()).first;
            if (healed_ptr != nullptr) {
                return static_cast<T*>(healed_ptr);
            }
        }
        return obj;
    }

    PtrGuard<T> get() {
        T* obj = this->getRaw();
        GCRegion* region = this->region.get();
        return PtrGuard<T>(obj, region);
    }

    PtrGuard<T> get() const {
        if (this->needHeal()) {
            auto healed = GCWorker::getWorker()->getHealedPointer(obj, obj_size, region.get());
            if (healed.first != nullptr) {
                return PtrGuard<T>(static_cast<T*>(healed.first), healed.second.get());
            }
        }
        return PtrGuard<T>(obj, region.get());
    }

    void* getVoidPtr() override {
        return reinterpret_cast<void*>(this->getRaw());
    }

    ObjectInfo getObjectInfo() override {
        if (ptrLock != nullptr) ptrLock->lockRead();
        void* obj_addr = this->getVoidPtr();
        unsigned int obj_size = this->obj_size;
        GCRegion* region = this->region.get();
        if (ptrLock != nullptr) ptrLock->unlockRead();
        return ObjectInfo{obj_addr, obj_size, region};
    }

    GCPtr_& operator=(const GCPtr_& other) {
        if (this != &other) {
            // GCPhase::EnterCriticalSection();
            if (this->obj != nullptr && this->obj != other.obj
                && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->getObjectInfo());
            }
            setInlineMarkState(other);
            if (ptrLock != nullptr) ptrLock->lockWrite();
            if constexpr (GCParameter::useCopiedMarkstate)
                this->obj = other.obj;
            else
                this->obj = const_cast<GCPtr_&>(other).getRaw();
            this->obj_size = other.obj_size;
            this->region = other.region;
            if (ptrLock != nullptr) ptrLock->unlockWrite();
            /*
             * 赋值运算符重载无需再次判别is_root，有且仅有构造函数需要
            if (GCWorker::getWorker()->is_root(this)) {
                this->is_root = true;
                GCWorker::getWorker()->addRoot(this);
            }
            */
            // GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    GCPtr_& operator=(std::nullptr_t) {
        if (this->obj != nullptr) {
            if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCPhase::EnterCriticalSection();
                GCWorker::getWorker()->addSATB(this->getObjectInfo());
                GCPhase::LeaveCriticalSection();
            }
            if (ptrLock != nullptr) ptrLock->lockWrite(true);
            this->obj = nullptr;
            this->obj_size = 0;
            this->region = nullptr;
            if (ptrLock != nullptr) ptrLock->unlockWrite();
        }
        return *this;
    }

    bool operator==(GCPtr_<T>& other) {
        return this->getRaw() == other.getRaw();
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr_(const GCPtr_& other) : GCPtrBase(other), obj_size(other.obj_size) {
        // std::clog << "Copy constructor" << std::endl;
        initPtrLock();
        GCPhase::EnterCriticalSection();
        // this->setInlineMarkState(other.getInlineMarkState());
        if (ptrLock != nullptr) ptrLock->lockWrite(true);
        if constexpr (GCParameter::useCopiedMarkstate)
            this->obj = other.obj;
        else
            this->obj = const_cast<GCPtr_&>(other).getRaw();
        this->region = other.region;
        if (ptrLock != nullptr) ptrLock->unlockWrite();
        this->is_root = GCWorker::getWorker()->is_root(this);
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        } else {
            GCWorker::getWorker()->addGCPtr(this);
        }
        GCPhase::LeaveCriticalSection();
    }

#if 0
    GCPtr_(GCPtr_&& other) noexcept : GCPtrBase(other), obj_size(other.obj_size) {
        // std::clog << "Move constructor" << std::endl;
        GCPhase::EnterCriticalSection();
        this->obj.store(other.obj.load());
        this->region = other.region;
        this->is_root = GCWorker::getWorker()->is_root(this);
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        /*
        other.obj = nullptr;
        other.obj_size = 0;
        other.region = nullptr;
        other.setInlineMarkState(MarkState::REMAPPED);
        */
        GCPhase::LeaveCriticalSection();
    }
#endif

    template<typename U>
    GCPtr_(const GCPtr_<U>& other) : GCPtrBase(other),
                                     obj_size(other.obj_size) {
        // this->setInlineMarkState(other.getInlineMarkState());
        initPtrLock();
        if constexpr (GCParameter::useCopiedMarkstate)
            this->obj = other.obj;
        else
            this->obj = static_cast<T*>(const_cast<GCPtr_<U>&>(other).getRaw());
        this->region = other.region;
        this->is_root = GCWorker::getWorker()->is_root(this);
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        } else {
            GCWorker::getWorker()->addGCPtr(this);
        }
        /*
        other.obj = nullptr;
        other.obj_size = 0;
        other.region = nullptr;
        other.setInlineMarkState(MarkState::REMAPPED);
        */
    }

    ~GCPtr_() override {
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCPhase::EnterCriticalSection();
            GCWorker::getWorker()->addSATB(this->getObjectInfo());
            GCPhase::LeaveCriticalSection();
        }
        if (is_root) {
            GCWorker::getWorker()->removeRoot(this);
        } else {
            GCWorker::getWorker()->removeGCPtr(this);
        }
    }
};

template<typename T>
class GCPtr : public GCPtr_<T> {
public:
    using GCPtr_<T>::GCPtr_;
    using GCPtr_<T>::operator=;

    PtrGuard<T> operator->() {
        return this->get();
    }

    PtrGuard<T> operator->() const {
        return this->get();
    }

    T& operator*() const {
        return *(this->get());
    }

    T& operator*() {
        return *(this->get());
    }

    void set(T* obj, const std::shared_ptr<GCRegion>& region = nullptr) {
        // 备注：当且仅当obj是新的、无中生有的时候才需要调用set()以注册析构函数和移动构造函数
        if (this->ptrLock != nullptr)
            this->ptrLock->lockWrite();
        this->obj = obj;
        this->obj_size = sizeof(*obj);
        this->region = region;
        if (this->ptrLock != nullptr)
            this->ptrLock->unlockWrite();
        if (obj == nullptr) return;
        GCWorker::getWorker()->registerObject(obj, sizeof(*obj));
        if (GCWorker::getWorker()->destructorEnabled()) {
            GCWorker::getWorker()->registerDestructor(obj,
                                                      [](void* self) { static_cast<T*>(self)->~T(); },
                                                      region.get());
        }
        if (GCParameter::enableMoveConstructor && region != nullptr) {
            region->registerMoveConstructor(obj,
                                            [](void* source_addr, void* target_addr) {
                T* target = static_cast<T*>(target_addr);
                new(target) T(std::move(*static_cast<T*>(source_addr)));
            });
        }
    }
};

template<>
class GCPtr<void> : public GCPtr_<void> {
public:
    using GCPtr_<void>::GCPtr_;
    using GCPtr_<void>::operator=;

    void set(void* obj, unsigned int obj_size,
             const std::shared_ptr<GCRegion>& region = nullptr,
             const std::function<void(void*)>& destructor = nullptr) {
        if (ptrLock != nullptr) ptrLock->lockWrite();
        this->obj = obj;
        this->obj_size = obj_size;
        this->region = region;
        if (ptrLock != nullptr) ptrLock->unlockWrite();
        if (obj == nullptr) return;
        GCWorker::getWorker()->registerObject(obj, obj_size);
        if (GCWorker::getWorker()->destructorEnabled() && destructor != nullptr) {
            GCWorker::getWorker()->registerDestructor(obj,
                                                      destructor,
                                                      region.get());
        }
    }
};

namespace gc {
    template<class T, class... Args>
    GCPtr<T> make_gc(Args&& ... args) {
        GCPtr<T> gcptr;
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (GCWorker::getWorker()->memoryAllocatorEnabled()) {
            auto pair = GCWorker::getWorker()->allocate(sizeof(T));
            obj = static_cast<T*>(pair.first);
            region = pair.second;
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        gcptr.set(obj, region);
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        return gcptr;
    }

    template<class T, class... Args>
    GCPtr<T> make_static(Args&& ... args) {
        GCPtr<T> gcptr(true);
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (GCWorker::getWorker()->memoryAllocatorEnabled()) {
            auto pair = GCWorker::getWorker()->allocate(sizeof(T));
            obj = static_cast<T*>(pair.first);
            region = pair.second;
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        gcptr.set(obj, region);
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        return gcptr;
    }

    void triggerGC() {
        GCWorker::getWorker()->triggerGC();
    }
    
#if ENABLE_FREE_RESERVED
    void freeReservedMemory() {
        // 该函数目前仅用于二级内存池的预留内存释放
        GCWorker::getWorker()->freeGCReservedMemory();
    }
#endif

#ifdef OLD_MAKEGC
    template<class T, class... Args>
    GCPtr<T> make_static(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (GCWorker::getWorker()->memoryAllocatorEnabled()) {
            auto pair = GCWorker::getWorker()->allocate(sizeof(T));
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
