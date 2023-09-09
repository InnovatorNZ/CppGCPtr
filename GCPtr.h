#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include <atomic>
#include "GCPtrBase.h"
#include "GCWorker.h"

template<typename T>
class PtrGuard {
private:
    T* ptr;
    GCRegion* region;
    bool owns;
    const bool relocationEnabled;

    struct DeferGuard_t {
        explicit DeferGuard_t() = default;
    };

public:
    static constexpr DeferGuard_t DeferGuard{};

    PtrGuard(T* ptr, GCRegion* region, DeferGuard_t) :
            ptr(ptr), region(region), owns(false),
            relocationEnabled(GCWorker::getWorker()->relocationEnabled()) {
    }

    PtrGuard(T* ptr, GCRegion* region) : PtrGuard(ptr, region, DeferGuard) {
        if (relocationEnabled)
            lock();
    }

    ~PtrGuard() {
        unlock();
    }

    PtrGuard(const PtrGuard&) = delete;

    PtrGuard(PtrGuard&&) = delete;

    void lock() {
        if (!owns) {
            region->inc_use_count();
            owns = true;
        }
    }

    void unlock() {
        if (owns) {
            region->dec_use_count();
            owns = false;
        }
    }

    T* get() const {
        return ptr;
    }

    T* operator->() const {
        return ptr;
    }

    T* value() const {
        return ptr;
    }
};

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

public:
    GCPtr() : obj(nullptr), obj_size(0) {
        is_root = GCWorker::getWorker()->is_root(this);
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
    }

    explicit GCPtr(bool is_root) : obj(nullptr), obj_size(0), is_root(is_root) {
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
    }

    explicit GCPtr(T* obj, const std::shared_ptr<GCRegion>& region = nullptr, bool is_root = false) {
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
        return PtrGuard<T>(this->getRaw(), this->region.get());
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

    PtrGuard<T> operator->() {
        return this->get();
    }

    PtrGuard<T> operator->() const {
        return this->get();
    }

    void set(T* obj, const std::shared_ptr<GCRegion>& region = nullptr) {
        // 备注：当且仅当obj是新的、无中生有的时候才需要调用set()以注册析构函数和移动构造函数
        this->obj = obj;
        this->obj_size = sizeof(*obj);
        this->region = region;
        GCWorker::getWorker()->registerObject(obj, sizeof(*obj));
        if (GCWorker::getWorker()->destructorEnabled()) {
            GCWorker::getWorker()->registerDestructor(obj,
                                                      [](void* self) { static_cast<T*>(self)->~T(); },
                                                      region.get());
        }
        if constexpr (GCParameter::enableMoveConstructor) {
            region->registerMoveConstructor(obj,
                                            [](void* source_addr, void* target_addr) {
                                                T* target = static_cast<T*>(target_addr);
                                                new(target) T(std::move(*static_cast<T*>(source_addr)));
                                            });
        }
    }

    void* getVoidPtr() override {
        return reinterpret_cast<void*>(this->getRaw());
    }

    ObjectInfo getObjectInfo() override {
        void* obj_addr = this->getVoidPtr();
        return ObjectInfo{obj_addr, obj_size, region.get()};
    }

    GCPtr& operator=(const GCPtr& other) {
        if (this != &other) {
            // GCPhase::EnterCriticalSection();
            if (this->obj != nullptr && this->obj != other.obj
                && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->getObjectInfo());
            }
            setInlineMarkState(other);
            if constexpr (GCParameter::useCopiedMarkstate)
                this->obj.store(other.obj.load());
            else
                this->obj = const_cast<GCPtr&>(other).getRaw();
            this->obj_size = other.obj_size;
            this->region = other.region;
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

    GCPtr& operator=(std::nullptr_t) {
        if (this->obj != nullptr) {
            if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCPhase::EnterCriticalSection();
                GCWorker::getWorker()->addSATB(this->getObjectInfo());
                GCPhase::LeaveCriticalSection();
            }
            this->obj = nullptr;
            this->obj_size = 0;
            this->region = nullptr;
        }
        return *this;
    }

    bool operator==(GCPtr<T>& other) {
        return this->getRaw() == other.getRaw();
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr(const GCPtr& other) : GCPtrBase(other), obj_size(other.obj_size) {
        // std::clog << "Copy constructor" << std::endl;
        GCPhase::EnterCriticalSection();
        // this->setInlineMarkState(other.getInlineMarkState());
        if constexpr (GCParameter::useCopiedMarkstate)
            this->obj.store(other.obj.load());
        else
            this->obj = const_cast<GCPtr&>(other).getRaw();
        this->region = other.region;
        this->is_root = GCWorker::getWorker()->is_root(this);
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

#if 0
    GCPtr(GCPtr&& other) noexcept : GCPtrBase(other), obj_size(other.obj_size) {
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
    GCPtr(const GCPtr<U>& other) : GCPtrBase(other),
                                   obj_size(other.obj_size) {
        // this->setInlineMarkState(other.getInlineMarkState());
        if constexpr (GCParameter::useCopiedMarkstate)
            this->obj.store(other.obj.load());
        else
            this->obj = static_cast<T*>(const_cast<GCPtr<U>&>(other).getRaw());
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
    }

    ~GCPtr() override {
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCPhase::EnterCriticalSection();
            GCWorker::getWorker()->addSATB(this->getObjectInfo());
            GCPhase::LeaveCriticalSection();
        }
        if (is_root) {
            GCWorker::getWorker()->removeRoot(this);
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
