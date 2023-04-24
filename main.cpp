#include <iostream>
#include "GCPtr.h"

class MyObject {
public:
    int a;
    double b;
    std::string c;
    GCPtr<MyObject> d;
    GCPtr<MyObject> e;
    float f;

    MyObject() : a(0), b(0.0) {
    }
};

GCPtr<MyObject> obj3;

int main() {
    using namespace std;
    GCPtr<MyObject> obj1 = gc::make_root<MyObject>();
    GCPtr<MyObject> obj2;
    obj2 = obj1;
    obj3 = obj2;
    {
        obj3->d = gc::make_gc<MyObject>();
        obj3->d->a = 173;
        obj3->d->d = gc::make_gc<MyObject>();
        obj3->e = gc::make_gc<MyObject>();
        obj3->e->f = 12.43;
    }
    cout << &obj1 << " " << &obj2 << " " << &obj3 << " " << &obj3->d << " " <<
         &obj3->e << " " << &obj3->d->d << endl;
    cout << obj3->e->f << endl;
    GCWorker::getWorker()->beginMark();
    GCWorker::getWorker()->printMap();
    // std::shared_ptr<MyObject> ptr = std::make_shared<MyObject>();
    // ptr->get(); ptr.get()->a;
    return 0;
}