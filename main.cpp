#include <iostream>
#include "GCPtr.h"

class MyObject {
public:
    int a;
    double b;
    std::string c;
    GCPtr<MyObject> d;

    MyObject() : a(0), b(0.0) {
    }
};

GCPtr<MyObject> obj3;

int main() {
    GCPtr<MyObject> obj1 = gc::make_gc<MyObject>();
    GCPtr<MyObject> obj2;
    obj2 = obj1;
    obj3 = obj2;
    std::cout << &obj1 << " " << &obj2 << " " << &obj3 << std::endl;
    GCWorker::getWorker()->printMap();
    GCWorker::getWorker()->beginMark();
    // std::shared_ptr<MyObject> ptr = std::make_shared<MyObject>();
    // ptr->get(); ptr.get()->a;
    return 0;
}