#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include "GCPtrBase.h"
#include "GCWorker.h"

template<typename T>
class GCPtr : public GCPtrBase {
    template<typename U>
    friend class GCPtr;

private:
    T* obj;
    unsigned int obj_size;
    bool is_root;
    std::shared_ptr<GCRegion> region;
    const int identifier_tail = GCPTR_IDENTIFIER_TAIL;

    bool needHeal() const {
        return this->obj != nullptr && GCWorker::getWorker()->relocationEnabled()
            && GCPhase::needSelfHeal(getInlineMarkState());
    }

    void selfHeal() {
        auto healed = GCWorker::getWorker()->getHealedPointer(this->obj, this->obj_size, region);
        if (healed.first != nullptr) {
            std::clog << "Healing GCPtr(" << this << ", " << MarkStateUtil::toString(getInlineMarkState()) <<
                ") from " << obj << " to " << healed.first << std::endl;
            this->obj = static_cast<T*>(healed.first);
            this->region = healed.second;
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

    GCPtr(T* obj, bool is_root, std::shared_ptr<GCRegion> region) : is_root(is_root) {
        GCPhase::EnterCriticalSection();
        this->obj = obj;
        this->obj_size = sizeof(*obj);
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
        std::clog << "Calling get() const on " << obj << ", temporarily disable selfheal" << std::endl;
        if (this->needHeal())
            return static_cast<T*>(GCWorker::getWorker()->getHealedPointer(this->obj, this->obj_size, region).first);
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

    unsigned int getObjectSize() const override {
        return obj_size;
    }

    std::shared_ptr<GCRegion> getRegion() const override {
        return this->region;
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            GCPhase::EnterCriticalSection();
            if (this->obj != nullptr && this->obj != other.obj && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->obj, this->obj_size);
            }
            this->obj = other.obj;
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
                GCWorker::getWorker()->addSATB(this->obj, this->obj_size);
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

    GCPtr(const GCPtr& other) {
        GCPhase::EnterCriticalSection();
        std::clog << "Copy constructor: " << this << std::endl;
        this->obj = other.obj;
        this->obj_size = other.obj_size;
        this->is_root = other.is_root;
        this->region = other.region;
        this->setInlineMarkState(other.getInlineMarkState());
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    GCPtr(GCPtr&& other) {
        GCPhase::EnterCriticalSection();
        std::clog << "Move constructor: " << this << std::endl;
        this->obj = other.obj;
        this->obj_size = other.obj_size;
        this->is_root = other.is_root;
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
            GCWorker::getWorker()->addSATB(this->obj, this->obj_size);
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
