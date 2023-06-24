#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include <atomic>
#include "GCPtrBase.h"
#include "GCWorker.h"

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

    void selfHeal() {
        auto healed = GCWorker::getWorker()->getHealedPointer(obj, obj_size, region.get());
        if (healed.first != nullptr) {
            std::clog << "Healing GCPtr(" << this << ", " << MarkStateUtil::toString(getInlineMarkState()) <<
                      ") from " << obj << " to " << healed.first << std::endl;
            this->obj = static_cast<T*>(healed.first);
            this->region = healed.second;
        } else {
            std::clog << "Healing non-forwarding GCPtr(" << this << ", " << MarkStateUtil::toString(getInlineMarkState())
                      << "): " << obj << std::endl;
        }
        this->setInlineMarkState(MarkState::REMAPPED);
    }

public:
    GCPtr() : obj(nullptr), obj_size(0), is_root(false) {
    }

    explicit GCPtr(T* obj) : GCPtr(obj, false) {
    }

    GCPtr(T* obj, bool is_root) : is_root(is_root), obj_size(sizeof(*obj)) {
        GCPhase::EnterCriticalSection();
        this->obj = obj;
        GCWorker::getWorker()->registerObject(obj, sizeof(*obj));
        if (GCWorker::getWorker()->destructorEnabled())
            GCWorker::getWorker()->registerDestructor(obj, [obj]() { obj->~T(); });
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    GCPtr(T* obj, bool is_root, const std::shared_ptr<GCRegion>& region) :
            is_root(is_root), obj_size(sizeof(*obj)) {
        GCPhase::EnterCriticalSection();
        this->obj = obj;
        this->region = region;
        if (GCWorker::getWorker()->destructorEnabled())
            GCWorker::getWorker()->registerDestructor(obj, [obj]() { obj->~T(); });
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
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
            return static_cast<T*>(GCWorker::getWorker()->getHealedPointer(obj, obj_size, region.get()).first);
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
        return ObjectInfo{ obj_addr, obj_size, region.get() };
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            GCPhase::EnterCriticalSection();
            if (this->obj != nullptr && this->obj != other.obj && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->getObjectInfo());
            }
            this->obj.store(other.obj.load());
            this->obj_size = other.obj_size;
            this->region = other.region;
            this->setInlineMarkState(other.getInlineMarkState());
            this->is_root = other.is_root;
            if (is_root)
                GCWorker::getWorker()->addRoot(this);
            GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    GCPtr& operator=(std::nullptr_t) {
        if (this->obj != nullptr) {
            GCPhase::EnterCriticalSection();
            if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->getObjectInfo());
            }
            this->obj = nullptr;
            this->obj_size = 0;
            this->region = nullptr;
            GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    bool operator==(GCPtr<T>& other) {
        return this->get() == other.get();
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr(const GCPtr& other) : is_root(other.is_root), obj_size(other.obj_size) {
        GCPhase::EnterCriticalSection();
        std::clog << "Copy constructor: " << this << std::endl;
        this->obj.store(other.obj.load());
        this->region = other.region;
        this->setInlineMarkState(other.getInlineMarkState());
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    GCPtr(GCPtr&& other) : is_root(other.is_root), obj_size(other.obj_size) {
        GCPhase::EnterCriticalSection();
        std::clog << "Move constructor: " << this << std::endl;
        this->obj.store(other.obj.load());
        this->region = std::move(other.region);
        this->setInlineMarkState(other.getInlineMarkState());
        other.obj = nullptr;
        other.obj_size = 0;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    template<typename U>
    GCPtr(GCPtr<U>&& other) : obj(other.obj), obj_size(other.obj_size), is_root(other.is_root) {
        this->setInlineMarkState(other.getInlineMarkState());
        this->region = std::move(other.region);
        other.obj = nullptr;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
    }

    ~GCPtr() override {
        GCPhase::EnterCriticalSection();
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCWorker::getWorker()->addSATB(this->getObjectInfo());
        }
        if (is_root) {
            GCWorker::getWorker()->removeRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }
};

namespace gc {
    template<class T, class... Args>
    GCPtr <T> make_gc(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (GCWorker::getWorker()->bitmapEnabled()) {
            auto pair = GCWorker::getWorker()->allocate(sizeof(T));
            obj = static_cast<T*>(pair.first);
            region = pair.second;
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        if (region == nullptr)
            return GCPtr<T>(obj);
        else
            return GCPtr<T>(obj, false, region);
    }

    template<class T, class... Args>
    GCPtr <T> make_root(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        std::shared_ptr<GCRegion> region = nullptr;
        if (GCWorker::getWorker()->bitmapEnabled()) {
            auto pair = GCWorker::getWorker()->allocate(sizeof(T));
            obj = static_cast<T*>(pair.first);
            region = pair.second;
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        if (region == nullptr)
            return GCPtr<T>(obj, true);
        else
            return GCPtr<T>(obj, true, region);
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
