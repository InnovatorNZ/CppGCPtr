#include <iostream>
#include "GCPtr.h"

class MyObject {
public:
    int a;
    double b;
    std::string c;
    GCPtr<MyObject> d;
    double f;
    GCPtr<MyObject> e;

    MyObject() : a(0), b(0.0) {
    }

    void setG(GCPtr<MyObject> _g) {
        this->g = _g;
    }

private:
    GCPtr<MyObject> g;
};

GCPtr<MyObject> obj3;

int main() {
    using namespace std;
    GCPtr<MyObject> obj2;
    {
        GCPtr<MyObject> obj1 = gc::make_root<MyObject>();
        obj2 = obj1;
    }
    obj3 = obj2;
    {
        obj3->d = gc::make_gc<MyObject>();
        obj3->d->a = 173;
        obj3->d->e = gc::make_gc<MyObject>();
        obj3->e = gc::make_gc<MyObject>();
        obj3->e->f = 12.43;
        GCPtr<MyObject> obj4 = gc::make_root<MyObject>();
        GCPtr<MyObject> obj5 = gc::make_root<MyObject>();
        //obj4->setG(obj5);     //还是要运行时判断是不是栈变量啊
        obj2->setG(gc::make_gc<MyObject>());
    }
    // cout << &obj1 << " " << &obj2 << " " << &obj3 << " " << &obj3->d << " " <<
    //     &obj3->e << " " << &obj3->d->d << endl;
    cout << obj3->e->f << endl;
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginMark();
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginSweep();
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->endGC();
    obj3->e->f = 114.514;
    cout << obj3->e->f << endl;
    // std::shared_ptr<MyObject> ptr = std::make_shared<MyObject>();
    // ptr->get(); ptr.get()->a;

    GCPtr<MyObject> obj5 = gc::make_root<MyObject>();
    {
        GCPtr<MyObject> obj4 = gc::make_root<MyObject>();
    }
    obj3 = nullptr;
    obj2 = nullptr;
    cout << (obj3 == nullptr) << " " << (obj2 == nullptr) << endl;
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginMark();
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginSweep();
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->endGC();
    GCPtr<MyObject> obj6 = gc::make_root<MyObject>();
    GCPtr<MyObject> obj7 = gc::make_root<MyObject>();
    {
        GCPtr<MyObject> obj8 = gc::make_root<MyObject>();
    }
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginMark();
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginSweep();
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->endGC();
    return 0;
}