## Lanugage
- [English](#english)
- [中文](#中文)
---

### English

# CppGCPtr: Smart Pointer based on Garbage Collection with Memory Defragmentation

In C++, define a GCPtr\<T> to enable smart pointers (like shared_ptr) but based on garbage collection.

#### A simple example:

```cpp
#include "GCPtr.h"		// include GCPtr header

int main () {
	// define a GCPtr, and use gc::make_gc<> to allocate new boject
	// equivalent to shared_ptr<MyObject> myObj = make_shared<MyObject>();
	// myObj is the object that you want to be managed by GC
	GCPtr<MyObject> myObj = gc::make_gc<MyObject>();
	
	myObj->a = 1;
	cout << myObj->a << endl;
	
	return 0;
}
```

<br/>

## How to install

1. Clone this repository locally

```bash
git clone --recursive https://github.com/innovatornz/CppGCPtr
```

Remember to always add the `--recursive` option, as this project contains submodules.

2. This project already contained a main.cpp example of how to use it. Run the following command in the project folder to run it<br/>

Under Linux and macOS:

```bash
g++ *.cpp CppExecutor/ThreadPoolExecutor.cpp -o CppGCPtr -std=c++20
./CppGCPtr
```

Make sure you are using gcc 13 and above to support C++20 (so you will need to replace g++ with g++-13 in the above command as appropriate, if you don't have it you can install it like this)

```bash
sudo apt install g++-13
```

For macOS install it like this

```bash
brew install g++-13
```

<br/>

Under Windows: Make sure you have Visual Studio 2019 and above (2022 recommended) installed the C++ workload. Run the following command in the project folder

```bash
mkdir build
cd build
cmake ..
```

Then double click on the generated CppGCPtr.sln file, open it in Visual Studio, click on Generate Solution and run.<br/>*(BTW, it is not recommended to use MinGW)*

3. If you want to reference this library directly in your existing project, please reference `CppGCPtr/GCPtr.h` in your project and delete the `CppGCPtr/main.cpp` file, then compile and run it as usual.

*Please note that this project is still in beta and not recommended for use in a production environment!*

---

A more complete example:

```cpp
#include <iostream>
#include "GCPtr.h"

class MyObject {
public:
	int a;
	double b;
	GCPtr<MyObject> c;
	GCPtr<MyObject> d;

	MyObject(double b_) : a(0), b(b_) {
	}
};

int main() {
	// Define a GCPtr and use gc::make_gc<> to allocate a new object
	// equivalent to shared_ptr<T> a = make_shared<>();
	GCPtr<MyObject> myObj = gc::make_gc<MyObject>(1.0);
	
	// Use `->` or `*` operator to access managed object
	myObj->c = gc::make_gc<MyObject>(0.5);
	(*myObj).a = 1;

	{
		GCPtr<MyObject> myObj2 = gc::make_gc<MyObject>(2.0);
		std::cout << myObj2->b << std::endl;	// output 2
		
		GCPtr<MyObject> myObj3 = gc::make_gc<MyObject>(3.0);
		myObj->c->c = myObj3;		// Point the outside myObj to myObj3 making myObj3 reachable
		
		myObj->c->d = gc::make_gc<MyObject>(4.0);
		GCPtr<MyObject> myObj4 = myObj->c->d;
		myObj4->b = 5.0;
	}
	
	gc::triggerGC();	// trigger GC manually
	Sleep(1000);		// Wait a second for GC to complete since GC runs in a seperate GC thread
	
	// At this time, myObj2 is freed, while myObj, myObj3 and myObj4 survive
	std::cout << myObj->a << std::endl;		// output 1
	std::cout << myObj->c->b << std::endl;  // output 0.5
	std::cout << myObj->c->c->b << std::endl;	// output 3
	std::cout << myObj->c->d->b << std::endl;	// output 5
	
	myObj = nullptr;	// explicitly assign to null
	gc::triggerGC();	// manually trigger gc, myObj, myObj3 and 4 are freed
	return 0;
}
```

An example of LRU using GCPtr:

```cpp
struct Node {
    int key, value;
    GCPtr<Node> prev, next;

    Node() : prev(nullptr), next(nullptr) {}

    Node(int key, int value) : key(key), value(value), prev(nullptr), next(nullptr) {
    }
};

class LinkedList {
private:
    GCPtr<Node> head;
    GCPtr<Node> tail;
public:
    LinkedList() {
        head = gc::make_gc<Node>(-2, 0);
        tail = gc::make_gc<Node>(-3, 0);
        head->next = tail;
        tail->prev = head;
    }

    void remove(GCPtr<Node> node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    void insert_head(GCPtr<Node> node) {
        GCPtr<Node> next = head->next;
        node->next = next;
        node->prev = head;
        next->prev = node;
        head->next = node;
    }

    GCPtr<Node> get_tail() {
        if (tail->prev == head) return nullptr;
        return tail->prev;
    }
};

class LRUCache {
private:
    const int capacity;
    GCPtr<std::unordered_map<int, GCPtr<Node>>> map;
    GCPtr<LinkedList> linkedList;

public:
    LRUCache(int capacity) : capacity(capacity) {
        map = gc::make_gc<std::unordered_map<int, GCPtr<Node>>>();
        linkedList = gc::make_gc<LinkedList>();
    }

    int get(int key) {
        auto it = map->find(key);
        if (it == map->end()) return -1;
        GCPtr<Node> node = it->second;
        linkedList->remove(node);
        linkedList->insert_head(node);
        return node->value;
    }

    void put(int key, int value) {
        auto it = map->find(key);
        if (it != map->end()) {
            GCPtr<Node> node = it->second;
            node->value = value;
            linkedList->remove(node);
            linkedList->insert_head(node);
            return;
        }
        if (map->size() >= capacity) {
            GCPtr<Node> del = linkedList->get_tail();
            linkedList->remove(del);
            map->erase(del->key);
        }
        GCPtr<Node> node = gc::make_gc<Node>(key, value);
        linkedList->insert_head(node);
        map->emplace(key, node);
    }
};
```

<br/>

## How does this GC work?

GCPtr works similarly to shared_ptr, but shared_ptr bases on reference counting, which has many limitations; whereas GCPtr runs the real deal, a garbage collection algorithm based on reachability analysis, and also a mobile garbage collection with memory defragmentation. Specifically, when you define a GCPtr, it means that this object will be managed by the GC (objects not surrounded by a GCPtr<> are not affected). A GC thread will start in the background. After you call gc::triggerGC() to trigger a GC, the GC thread will be notified and run garbage collected according to the following process:

#### 1\. Preparation

During the preparation phase, the GC thread does some prep work, resetting some gc data, flipping the current tag state, etc. This phase does not take much time and does not suspend the application thread.

#### 2\. Initial marking phase

In the initial marking phase, the GC thread will mark all the GCPtrs in the gc root, representing that all gc roots are alive. Subsequent reachability analysis will be recursively scanned on the root. This phase suspends all operations of the application thread against the gc root (e.g., creating GCPtr local variables), but other operations are not affected.<br/>Typically, a gc root contains local, global, and static variables. However, due to the nature of C++, in order for GCPtr to coexist with raw pointers, all objects that are not in the memory region that managed by GCPtr are treated as a gc root and is permanently live (unless it destructs itself).

#### 3\. Concurrent marking phase

In the concurrent marking phase, the GC thread will continue to mark all referenced objects on the marked gc root; this marking will be performed a depth-first search, specifically, the GC thread will scan all the values of the current object's memory, traversing them according to the memory alignment (64-bit is 8 bytes, 32-bit is 4 bytes). GCPtr will take up an additional 8 bytes to store its identifiers (magic values, object header 0x1f1e33fc, object tail 0x3e0e1cc). If these two magic values are found on this object, the object it points to will be identified and scanning will continue recursively until all the objects have been marked.

In GCPtr, three states, Remapped, M0 and M1, will be used to represent the marking state of an object, where M0 and M1 represent being marked, two states are used interchangeably; Remapped represents a new or relocated object that is not generated during GC, and thus a surviving Remapped object deserves to be set to M0/M1 during GC.

Since the concurrent marking phase runs in parallel with and does not suspend the application thread, reference changes may occur during the marking process. In order to ensure the correctness especially avoiding missing marking of living objects, the GC thread uses a three-color marking strategy based on a deletion barrier, i.e., Snapshot-at-the-beginning (SATB). When a deletion occurs (e.g., a GCPtr is explicitly set to nullptr, or a GCPtr destructs), the deleted object is added to the SATB queue and will be remarked in the subsequent remarking phase.

#### 4\. Remarking phase

In the re-marking phase, the objects that entered the SATB queue are re-scanned and re-marked. After this phase all surviving objects will be correctly marked. This phase will suspend the application threads. (Precisely, all operations on GCPtr are suspended).

In addition, all new objects created during the whole GC phase will be considered alive. They will be processed in the next round of GC (This is also known as floating garbage).

#### 5\. Relocation set selection phase

In the select-relocation-set phase, the GC thread scans all managed memory regions, specifically, all the memory areas allocated by calling gc::make_gc<>(). There are four types of memory region: mini, small, medium, and large, depending on the size of the object. The small region defaults to 2MB, storing objects with a size of 24 bytes to 16KB; the medium region defaults to 32MB, storing objects with a size of 16KB to 1MB; the large region stores all objects larger than 1MB, each object occupies the whole region; and the mini region defaults to a 256KB piece, storing objects with size less than 24 bytes.

When calling gc::make_gc, memory will be allocated from the corresponding type of region according to the size of the object. Inside each region, allocations based on pointer collision method, that is, starting from the currently allocated offset to the new object. Therefore, memory fragmentation will occur if an object is dead. If no region has free space that meets the requirements, a new region will be allocated.

The gc thread will traverse all regions and determine the fragmentation ratio and free ratio of this region. A region has more than 1/4 of the memory fragmentation and less than 1/4 of the free space will be added to relocation set by default. All objects inside this region will be reallocated to other regions in next phase to achieve memory defragmentation (a.k.a. memory compression).

#### 6\. Concurrent relocation phase

In the concurrent relocation phase, all the regions that were selected will be relocated. This phase does not suspend the application threads. The GC threads will scan all regions with multi-thread, traversing their marking bitmaps (GCBitMap) or marking hash tables (GCRegionalHashMap) in each region to find out all the surviving objects and relocating these objects to the other region. The original region will be freed after relocation done.

Since int the concurrent relocation phase, gc threads and application threads are  parallel, the following two issues are raised:

- Contested access: If a live object is relocated and an application thread happens to write to this object, a thread contention problem arises; obviously, the address after relocation is the correct address to write to. When the application thread finds out the object it accesses is inside the relocation set, it will take the initiative to relocate it first before accessing it. If the GC threads are also competing to relocate the object, a strategy similar to Compare-And-Swap will be used to ensure that only one thread is able to relocate the object successfully.
- Reference update: When an object is relocated, obviously its memory address has also changed; therefore, all pointers stored in GCPtr need to be updated. This address is lazy updated. Specifically, when a live object is relocated, it leaves a record in a forwarding table, key is the old address and the value is the new address. When an application thread accesses a GCPtr object, it first determines whether it needs to perform a pointer update. If yes, it will access the forwarding table of the region and look for the corresponding key-value pair; if no key-value pair is founded, it means that the object pointed to by this GCPtr isn't relocated, so there is no need to update the pointer; if it does find it, it will update the pointer. If there is no application thread that access a relocated object, the pointer update will be performed by the GC thread in the next GC round.<br/>
Here's a noteworthy point: how do you determine whether a GCPtr needs to access the forwarding table to perform a pointer update? It would obviously be very performance intensive to access the forwarding table every time to determine if a pointer update is needed. Here the following rules will be followed:
  - If the marking state of an object is Remapped, it means it must NOT need to perform a pointer update;
  - If the marking state of an object is M0/M1: 
    - If it's currently in the marking phase (including initial, concurrent, and re-marking subphases), the objects survived in the last gc round needs to perform a pointer update; that is, if the current mark state is M0, the GCPtr with state M1 needs to be updated, and if it is currently M1 the state M0 needs to be updated;
    - If it's not currently in the marking phase (including relocation set selection, concurrent relocation, preparation, wrap-up phases, and non-GC periods), it is the current round of surviving objects that need to perform the pointer update; that is, if the currently surviving objects are marked as M0, the GCPtr with state M0 needs to perform a pointer update, and if it is currently M1 then the state M1 needs to be updated;
    - When the pointer update is complete (including the forwarding table tells that no update is needed), set the object mark state to Remapped.

    The pointer update using the above strategy is strictly correct. The GCPtr class adds a field to maintain this mark state to improve memory locality as this variable is frequently accessed (called 'inlineMarkState'). It is worth mentioning that the idea here is very similar to the "coloring pointer" and "self-heal" of ZGC. You can refer to the working principle of ZGC if you are interested.

#### 7\. Wrap-up phase

At this stage, the GC thread performs some finishing work after the GC is completed, including resetting the count of surviving objects in each region, recycling temporary variables, etc. This phase does not take much time and does not suspend application threads. When this phase is over, the current GC round is finished.<br/><br/>

## Frequently Asked Q\&A

1. Q: Can I manually newing an object and handing the raw pointer to GCPtr to manage?<br/>
A: No. All objects managed by GCPtr<> must be created by gc::make_gc<>(). Constructing a GCPtr directly from a raw pointer is not supported. However, you can construct a new GCPtr from an existing GCPtr (i.e., a copy construction of GCPtr).

2. Q: How stable is GCPtr? Can it be used in a production environment?<br/>
A: GCPtr is not recommended for production use. This project is still in beta. Please feel free to ask by opening an issue or contact developer directly.

3. Q: How is the performance of GCPtr? Does using GCPtr affect the performance of application threads?<br/>
A: This is a case-by-case discussion. Usually, the performance complaint about garbage collection is that it causes Stop-the-World, which means that all application threads are stopped. However, in GCPtr, all the application threads that need to be suspended are only for GCPtr operations (constructing and destructing GCPtr) and only occur in the initial marking, re-marking, and relocation set selection phases. The rest of the cases do not require suspension at all. Experiment shows that even with very sick data (the test case of constantly constructing and destructing GCPtr), the longest suspension time will not be more than 5ms, and the majority of cases are within 1ms.<br/>Another major performance impact point is caused by the delete barrier and read barrier. The delete barrier only works during the concurrent marking phase, so it generally has little impact. The read barrier works through the process time, especially when updating pointers after completing a GC round, and despite the policy of determining whether an update is needed based on the mark state, there will always be some loss. In addition, all GCPtrs belonging to a gc root are added to a hash set (root set), which is also a major performance impact point. Experimental data indicates that using GCPtr generally causes a performance degradation of at least about 30% on application performance, so it is not recommended to apply GCPtr in performance-hardened scenarios.

## Other points to note

1. Please assign initial values to all member variables of the classes managed by GCPtr, especially those of array types, either zero or other initial values. Please also assign nullptr to pointer type member variables or perform new operation accordingly (but no need to assign initial value to the newly created memory space). This is because the GC thread determines whether it is a GCPtr or not based on the magic value. If you don't want to do this, please enable GCParameter::fillZeroForNewRegion to avoid the potential crash. (Problems are more likely to occur under Linux/macOS, less likely under Windows).

2. GCPtr does not currently support direct management of array type. Please consider using std::vector or similar data structures.

3. Do not take raw pointer out from GCPtr. If an object is relocated while its raw pointer is still in use, this will result in an unpredictable crash risk as old address is invalid. If you do need to use the raw pointer, please takes out its PtrGuard first, then takes the raw pointer from the PtrGuard, and make sure that the lifecycle of the PtrGuard covers the lifecycle of the raw pointer, as follows:

```cpp
GCPtr<MyObject> myObj = gc::make_gc<MyObject>();
{
	PtrGuard<MyObject> ptr_guard = myObj.get();
	MyObject* raw_ptr = ptr_guard.get();
	// code processing with raw_ptr
	// make sure the lifecycle of ptr_guard covers raw_ptr's
}
```

PtrGuard ensures that the region of the object not be relocated during its lifecycle.<br/>

## Parameter Interpretation

GCPtr supports tuning parameters. These parameters are in `GCParameter.h` and have corresponding explanations. Some of the important parameters are shown here.

**enableConcurrentGC**: whether to enable GC thread. If this option is disabled, the GC process will be performed directly by the application thread. It is recommended to enable it.

**enableMemoryAllocator**: whether to enable memory allocator. If this option is disabled, all memory allocation will be redirected to new and malloc provided by the operating system, which is a prerequisite for enabling mobile reclamation and correctly determining the gc root, so it is recommended to enable it.

**enableRelocation**: Enables or disables memory defragmentation, i.e., mobile reclamation that includes object reallocation. If this option is disabled, objects will not be relocated. It is recommended to enable it. If you explicitly don't need memory defragmentation, you can disable it, but please note that this will result in a region not being emptied as long as there is a living object in that region, thus wasting memory. (This can be optimized, if you need it you can ask the group).

**enableParallelGC**: if or not enable multi-threaded garbage collection, GC threads will open a thread pool to optimize time-consuming GC tasks. It is recommended to enable it.

**enableDestructorSupport**: whether or not to call the destructor function of an object when it is recycled. If your GCPtr-managed class contains bare pointers or STL smart pointers that need to be manually destructed in the destructor function, you must enable this option to prevent memory leaks. Otherwise, it is recommended to disable this option as calling the destructor will cause some performance loss.

**useRegionalHashMap**: for GC to adopt the object labeling state, whether to use a bitmap or a hash table, the default is to disable that is to use a bitmap, to enable that is to use a hash table. The two data structures have their own advantages and disadvantages, the bitmap is inherently thread-safe, for thread competition is more advantageous, but it is more memory, the size and the heap size proportional to the size of the hash table is less memory occupied, the size and the number of objects proportional to the size of the hash table, but does not have the thread security need to add locks, and the calculation of the hash also need to consume a certain amount of CPU. here it is recommended that the user for the enable and disable try to choose a higher performance of one. high performance one.

**useInlineMarkState**: if or not record the object mark state in GCPtr. This inline mark state is usually used for determining whether pointer self-healing is needed, and for skipping marked objects. This option must be enabled if you want to enable object relocation.

**useSecondaryMemoryManager**: Whether to enable the secondary memory pool. If this option is disabled, each new region will be allocated directly from the system malloc; if it is enabled, the new region will be allocated from this pool. Enabling this option avoids frequent mallocs to the system, reuses allocated memory, and improves memory allocation performance by a certain amount (about 10-15%). However, the current implementation does not yet support freeing reserved memory.

**enableMoveConstructor**: whether to call the move constructor of an object when it is reallocated. If enabled, when an object is reallocated to another region, the object's move constructor will be called instead of memcpy (see std::vector's expansion procedure). Do not enable this option, the current implementation does not support circular references. Please add the group at the end of the article if you need it.

**useConcurrentLinkedList**: if or not use unlocked linked list to manage region, otherwise std::deque will be used. enable unlocked linked list is more efficient for adding and deleting region, but it can't support multi-threaded recycling. It is not recommended to enable this option.

**deferRemoveRoot**: if or not defer removing a GCPtr from the root set when it is destructed. enabling this option improves the performance when GCPtr is destructed, but increases the memory usage of the root set. It is disabled by default.

**suspendThreadsWhenSTW**: Whether or not to suspend user threads during STW (both retagging and selecting a transfer set). If disabled, read and write locks are used and only operations against GCPtr are blocked. This option is only supported on Windows and is disabled by default, it is not recommended to enable it as it is not necessary, but you can enable it to try it out if you run into problems.

**enableHashPool**: Enable or disable the pooling scheme that takes a hash of thread ids. This option will work in several places, such as allocating new regions, memory pools, etc. If enabled, the pooling scheme will be applied to every access to the thread id. If enabled, it will alleviate thread contention by spreading it out as much as possible based on the thread id for each access to a variable shared by a thread. It is recommended to enable it, but you can disable it if your application threads are single-threaded.

**immediateClear**: try to clear objects that are already garbage after one round of recycling, otherwise they will be recycled after 2 to 3 rounds. Enabling this option increases the performance overhead of garbage collection, but frees up memory faster.

**doNotRelocatePtrGuard**: Disable transfers to any region that already has a PtrGuard reference. If disabled, it will spin wait until the PtrGuard is destructed. It is recommended to enable this option if you have a PtrGuard with a fairly long lifecycle in your code.

**zeroCountCondition**: If the currently transferred region contains a PtrGuard pointing to it, the GC thread will sleep until all PtrGuards are destructed, otherwise the GC thread will spin and wait. If there are more PtrGuards, it can reduce the CPU consumption of GC thread spin, but it will increase the performance consumption every time when constructing PtrGuard including removing pointers (because of the need of thread communication via condition variables). Disabled by default.

**enablePtrRWLock**: Use read/write locks to ensure thread safety for several variables of GCPtr. Enable this option to make GCPtr thread-safe, disable it if you don't need it. It is recommended to disable it.

**fillZeroForNewRegion**: zero-fill memory for all new regions. Enabling this option resolves crashes caused by user threads not initializing class member variables. Disabled by default.

**waitingForGCFinished**: the application thread will wait for the GC thread to finish all its work before continuing, which is really a full Stop-the-World garbage collection. Enable this option for debugging only if your program is encountering problems, otherwise, please disable it.

**bitmapMemoryFromSecondary**: the memory space used by bitmaps also comes from the secondary memory pool. Enabling this option can speed up memory allocation for bitmaps, but may cause fragmentation of the secondary memory pool.

**TINY_OBJECT_THRESHOLD**: The object size (upper limit) of the mini object. Default 24 bytes.

**TINY_REGION_SIZE**: The size of the mini-object's region. Default 256KB.

**SMALL_OBJECT_THRESHOLD**: The object size (upper limit) of the mini-object. Default 16KB.

**SMALL_REGION_SIZE**: The size of the region of the mini-object. Default 2MB.

**MEDIUM_OBJECT_THRESHOLD**: Object size (upper limit) for medium objects. Default 1MB.

**MEDIUM_REGION_SIZE**: Size of the region for medium objects. Default is 32MB.

**secondaryMallocSize**: the size of memory reserved for a single request to the operating system by the secondary memory pool. Default is 8MB.

**evacuateFragmentRatio and evacuateFreeRatio**: when the fragmentation ratio of a region is larger than evacuateFragmentRatio and the free space is smaller than evacuateFreeRatio, the region will be determined to need to be transferred. The default is one quarter (0.25).

***Just leave the rest of the parameters not shown at default***


---
### 中文

# CppGCPtr：基于移动式、低停顿垃圾回收的智能指针
在C++中，定义一个GCPtr&lt;T&gt;来启用基于垃圾回收运行的智能指针，用法就像shared_ptr一样。
#### 一个简单示例：
```cpp
#include "GCPtr.h"		// 包含GCPtr的头文件

int main () {
	// 定义一个GCPtr，并使用gc::make_gc<>分配新对象
	// 写法等同于shared_ptr<MyObject> myObj = make_shared<MyObject>();
	// MyObject则是你自己定义的类
	GCPtr<MyObject> myObj = gc::make_gc<MyObject>();
	
	myObj->a = 1;
	cout << myObj->a << endl;
	
	return 0;
}
```
<br/>

## 如何引用这个库
1. 克隆此仓库到本地
```bash
git clone --recursive https://gitee.com/innovatornz/CppGCPtr
```
记住务必加上`--recursive`选项，由于此项目包含子仓库

2. 本项目已附带了一份main.cpp示例如何使用。如果你想直接编译运行本项目附带的示例，在项目文件夹下运行如下命令<br/>

Linux和macOS下：
```bash
g++ *.cpp CppExecutor/ThreadPoolExecutor.cpp -o CppGCPtr -std=c++20
./CppGCPtr
```
请确保你使用gcc 13及以上的版本以支持C++20（因此你需要视情况将上述命令的g++替换为g++\-13，若你没有可以像这样安装）
```bash
sudo apt install g++-13
```
macOS如下安装
```bash
brew install g++-13
```
<br/>

Windows下：
请确保你安装了Visual Studio 2019及以上的版本（推荐2022），并安装了C++工作负载。在项目文件夹下运行如下命令
```bash
mkdir build
cd build
cmake ..
```
然后双击生成的CppGCPtr.sln文件，在Visual Studio中打开，点击生成解决方案，然后右击项目解决方案视图内的CppGCPtr并设为启动项目，点击运行即可<br/>*（另外，不推荐在Windows下使用mingw编译器编译本项目，因为有问题）*

3. 如果你想直接在你已有的项目中引用本库，请在你的项目中引用`CppGCPtr/GCPtr.h`，并删除`CppGCPtr/main.cpp`文件，然后按照老样子编译运行即可

*请注意，此项目仍在测试阶段，有许多不完善之处，不推荐在生产环境中使用此项目！*

---
一个更加完善的示例说明如何使用：
```cpp
#include <iostream>
#include "GCPtr.h"

class MyObject {
public:
	int a;
	double b;
	GCPtr<MyObject> c;
	GCPtr<MyObject> d;

	MyObject(double b_) : a(0), b(b_) {
	}
};

int main() {
	// 定义一个GCPtr，并使用gc::make_gc<>分配新对象
	// 用法等同于shared_ptr<T> a = make_shared<>();
	GCPtr<MyObject> myObj = gc::make_gc<MyObject>(1.0);
	
	// 使用指针运算符(->)、解引用运算符(*)访问被管理的类的成员变量
	myObj->c = gc::make_gc<MyObject>(0.5);
	(*myObj).a = 1;

	{
		GCPtr<MyObject> myObj2 = gc::make_gc<MyObject>(2.0);
		std::cout << myObj2->b << std::endl;	// 输出2
		
		GCPtr<MyObject> myObj3 = gc::make_gc<MyObject>(3.0);
		myObj->c->c = myObj3;		// 将外面的myObj的指针指向myObj3，使得myObj3可达
		
		myObj->c->d = gc::make_gc<MyObject>(4.0);
		GCPtr<MyObject> myObj4 = myObj->c->d;
		myObj4->b = 5.0;
	}
	
	gc::triggerGC();	// 手动触发GC
	Sleep(1000);		// 由于GC是并行进行的，这里等待一秒先让GC完成
	
	// 此时myObj2被回收，而myObj、myObj3和myObj4存活
	std::cout << myObj->a << std::endl;		// 输出1
	std::cout << myObj->c->b << std::endl;  // 输出0.5
	std::cout << myObj->c->c->b << std::endl;	// 输出3
	std::cout << myObj->c->d->b << std::endl;	// 输出5
	
	myObj = nullptr;	// 显式的指定为null
	gc::triggerGC();	// 手动触发GC，myObj、myObj3和4也被回收
	return 0;
}
```
一个使用GCPtr的LRU缓存代码示例：
```cpp
struct Node {
    int key, value;
    GCPtr<Node> prev, next;

    Node() : prev(nullptr), next(nullptr) {}

    Node(int key, int value) : key(key), value(value), prev(nullptr), next(nullptr) {
    }
};

class LinkedList {
private:
    GCPtr<Node> head;
    GCPtr<Node> tail;
public:
    LinkedList() {
        head = gc::make_gc<Node>(-2, 0);
        tail = gc::make_gc<Node>(-3, 0);
        head->next = tail;
        tail->prev = head;
    }

    void remove(GCPtr<Node> node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    void insert_head(GCPtr<Node> node) {
        GCPtr<Node> next = head->next;
        node->next = next;
        node->prev = head;
        next->prev = node;
        head->next = node;
    }

    GCPtr<Node> get_tail() {
        if (tail->prev == head) return nullptr;
        return tail->prev;
    }
};

class LRUCache {
private:
    const int capacity;
    GCPtr<std::unordered_map<int, GCPtr<Node>>> map;
    GCPtr<LinkedList> linkedList;

public:
    LRUCache(int capacity) : capacity(capacity) {
        map = gc::make_gc<std::unordered_map<int, GCPtr<Node>>>();
        linkedList = gc::make_gc<LinkedList>();
    }

    int get(int key) {
        auto it = map->find(key);
        if (it == map->end()) return -1;
        GCPtr<Node> node = it->second;
        linkedList->remove(node);
        linkedList->insert_head(node);
        return node->value;
    }

    void put(int key, int value) {
        auto it = map->find(key);
        if (it != map->end()) {
            GCPtr<Node> node = it->second;
            node->value = value;
            linkedList->remove(node);
            linkedList->insert_head(node);
            return;
        }
        if (map->size() >= capacity) {
            GCPtr<Node> del = linkedList->get_tail();
            linkedList->remove(del);
            map->erase(del->key);
        }
        GCPtr<Node> node = gc::make_gc<Node>(key, value);
        linkedList->insert_head(node);
        map->emplace(key, node);
    }
};
```
<br/>

## 此GC如何工作？

GCPtr的工作原理和shared_ptr类似，但shared_ptr基于引用计数进行，有许多限制；而GCPtr运行的是货真价实的、基于可达性分析的垃圾回收算法，而且还是带有内存碎片整理的移动式垃圾回收。
具体来说，当你定义了一个GCPtr后，代表此对象将被GC管理（没有被GCPtr<>包围的对象不会受GC影响）。后台会启动一个GC线程。当你调用gc::triggerGC()触发一次GC后，GC线程将被唤醒，并按如下流程进行垃圾回收：

#### 1. 准备阶段
准备阶段GC线程会做一些前置工作，例如重置gc数据、翻转当前标记状态等。此阶段不会耗费多少时间，不会阻塞应用线程。

#### 2. 初始标记阶段
在初始标记阶段，GC线程会对所有的gc root中的GCPtr进行标记，代表所有gc root是存活的，并且后续的可达性分析也将在root上进行扫描。这个阶段会阻塞住应用线程所有针对gc root的操作（例如，创建GCPtr局部变量），但其它操作不受影响。<br/>
通常来说，gc root包含局部变量、全局变量和静态变量。不过，由于C++的特性所致，为了让GCPtr能够和裸指针共存，所有不在被GCPtr所管理的内存区域里的对象都将视为gc root并永远存活（除非它自己析构了）。

#### 3. 并发标记阶段
在并发标记阶段，GC线程会在已标记的gc root上继续对所有被引用的对象进行标记；这个标记将采用深度优先搜索进行，具体来说，GC线程会扫描当前对象的所有内存的值，按照内存对齐原则进行遍历（64位就是8字节，32位就是4字节）。GCPtr会占用额外的8字节存储其标识符（魔法值，对象头0x1f1e33fc、对象尾0x3e0e1cc），如果在这个对象上找到了这两个魔法值，则会识别出其所指向的对象，并继续递归进行此轮扫描，直到所有对象都被标记了。

在GCPtr中，会用Remapped、M0和M1三种状态表示一个对象的标记状态，其中M0和M1表示被标记，两种状态交替使用；Remapped则代表非GC期间产生的新对象或被转移的对象，因此存活的Remapped对象理应在GC期间被置为M0/M1。

由于并发标记阶段与应用线程并行运行、不阻塞应用线程，因此可能会在标记的过程中发生引用变更。为了保证标记的正确性并不漏标，GC线程会采用基于删除屏障的三色标记策略，即“原始快照”（Snapshot-at-the-beginning, SATB）。当发生了删除行为（例如，显式地将某GCPtr置为nullptr，或某GCPtr析构）时，被删除的对象会被加入SATB队列，并在后续的重标记阶段重新标记。

#### 4. 重标记阶段
在重标记阶段，会对并发标记阶段中因触发删除屏障而进入SATB队列中的对象进行重新扫描与标记。经过了重标记阶段后，所有存活对象都将被正确标记。这个过程将会阻塞应用线程。（准确的说是阻塞所有针对GCPtr的操作）。

另外，GC过程中新产生的对象将一律视为存活，即便该对象很快就死亡也需要在下一轮GC中才被回收（也就是所谓的浮动垃圾）。

#### 5. 选择转移集合阶段
在选择转移集合阶段，GC线程将扫描所有的被管理内存区域。这个“被管理内存区域”代表所有调用gc::make_gc<>()分配给GCPtr所指向对象的内存区域。每一片内存区域按照对象大小，区分迷你、小、中、大三种内存区域（以下称为region）。小region默认为2MB一片，存放大小24字节～16KB的对象；中region默认为32MB一片，存放大小16KB～1MB的对象；大region则存放所有大于1MB的对象，每个对象独享一块region；迷你region默认为256KB一片，存放大小小于24字节的对象。

当调用gc::make_gc创建GCPtr时，将根据对象大小从相应种类的region中分配内存。分配按照指针碰撞法，也就是从当前已分配的偏移量开始，分配给新对象。因此，如果一个region中已分配区域中的对象已死亡的话，将会产生内存碎片。如果没有region有符合要求的空余空间，将分配新region。

选择转移集合将遍历所有region，根据一定条件判断此region的碎片比率和空闲比率。具体地说，当一片region中内存碎片量超过1/4，并且剩余空间小于1/4时，将会触发转移，也就是说，这个region中的所有对象将会被统统重新分配至其它region中，以实现内存碎片整理（也就是内存压缩）。转移具体会在并发转移阶段进行，这个阶段只会进行挑选要对哪些region进行转移。

#### 6. 并发转移阶段
在并发转移阶段，所有在上一轮被选中转移的region将进行转移。这个阶段是完全并行的，不阻塞应用线程。GC线程将启用线程池，多线程扫描所有region，每个region中对其标记位图（GCBitMap）或标记哈希表（GCRegionalHashMap）进行遍历，根据标记阶段找出所有存活对象，并将这些对象重新分配至其它region，以实现内存碎片整理。当所有对象都被重分配完后，原region将被释放。

由于转移阶段和应用线程完全并行，因此会引发以下两个问题：
- 竞争访问：如果一个存活对象被转移，而应用线程正好需要修改这个对象的数据，这时会产生线程竞争问题；显然，被转移后的对象才是正确的写入位置。当应用线程发现其要访问的对象位于需要被转移集合中，则会主动将其先行转移再访问。如果此时GC线程也在竞争地转移此对象，则会采用类似Compare-And-Swap的策略，保证只有一个线程能够转移成功。
- 引用更新：当一个对象被转移之后，显然它的内存地址也发生了变化；因此，需要将所有GCPtr中存放的指针也更新成转移后的新地址。由于GC线程并不管理所有GCPtr的集合（可启用），因此，所有指针更新采用懒更新的方式。具体来说，当一个存活对象被转移后，其会在原region里的一张转发表内留下记录，key为原地址、value为新地址。当应用线程访问一个GCPtr的对象时，会先判断是否需要执行指针的更新。如果判断需要更新，则会访问其所在region的转发表，寻找key为其当前地址；如果没有找到，代表这个GCPtr指向的对象没有被转移，无需更新指针；如果找到了，则将此指针更新。如果在一整个期间没有任何应用线程访问某个被转移存活对象的GCPtr，则会在下一次GC标记的时候由GC线程执行指针更新。<br/>
这里有一个值得注意的点：如何判断某个GCPtr是否需要访问转发表执行指针更新？如果每次都要访问转发表确定是否需要更新指针，显然是非常消耗性能的。这里会遵循以下规则：
	- 如果某对象的标记状态为Remapped，则代表它一定不需要执行指针更新；
	- 如果某对象的标记状态为M0/M1：
		- 如果当前在标记阶段（初始、并发、重标记三个子阶段都算），需要执行指针更新的是上一轮存活的对象；也就是，如果当前存活的对象用M0来标记，则状态为M1的GCPtr需要更新，如果当前是M1则状态为M0的需要更新；
		- 如果当前不在标记阶段（选择转移集合、并发转移、准备、收尾阶段以及非GC期间），需要执行指针更新的是本轮存活的对象；也就是，如果当前存活的对象是用M0来标记，则状态为M0的GCPtr需要执行指针更新，如果当前是M1则状态为M1的需要更新；
	- 当完成了指针更新后（包括访问转发表发现无需更新），重置对象标记状态为Remapped

	采用以上策略的指针更新是严格正确的，不会发生遗漏。由于对象标记状态在此策略下会需要频繁访问，因此GCPtr添加了专门用于维护此标记状态的字段以提升内存局部性（即内联标记）。（值得一提的是，这里与ZGC的“染色指针”和“指针自愈”是非常类似的思想，有兴趣的可以参考ZGC的工作原理）。

#### 7. 收尾阶段
这个阶段GC线程会执行一些GC完成后的收尾工作，包括重置每个region的存活对象计数，清空临时变量等。此阶段不会耗费多少时间，不会阻塞应用线程。当此阶段结束后，一轮GC也就走完了。
<br/><br/>

## 常见Q&A
1. Q: 能否通过手动new一个对象，并把指针交给GCPtr管理以启用垃圾回收？<br/>
A: 否。所有被GCPtr<>管理的对象必须通过gc::make_gc<>()创建，不支持直接从对象指针构造GCPtr。但是你可以从一个已有的GCPtr构造新的GCPtr（也就GCPtr的拷贝构造）。

2. Q: GCPtr的稳定性如何？能否应用于生产环境？<br/>
A: 不建议将GCPtr应用于生产环境。此项目现在仍在开发与测试阶段，有许多不稳定和不完善的地方。欢迎加群或开issue提出问题。群号见本ReadMe末尾。

3. Q: GCPtr的性能如何？使用GCPtr是否会影响应用线程的运行速度？<br/>
A: 这个问题需要分情况讨论。通常，对于垃圾回收的性能谴责点一般在于其会造成Stop-the-World，也就是造成全部应用线程停顿。不过，在GCPtr中，所有需要阻塞应用线程的地方仅仅针对GCPtr的操作（构造、析构GCPtr），而且只有在初始标记、重标记、选择转移集合三个阶段才会阻塞。其余情况都完全不需要阻塞。实践也证明，即便是非常变态的实验数据（不断构造、析构GCPtr的测试用例），最长阻塞时间也不会超过5ms，绝大多数情况在1ms以内。<br/>
另一个主要性能影响点是删除屏障和读屏障造成的。删除屏障只会在并发标记阶段起作用，因此一般影响不大（但如果并发标记过程很长导致删除屏障频繁触发也会有点影响）。读屏障则会一直起作用，尤其是当完成一轮GC后的指针更新，尽管有根据标记状态判断是否需要更新的策略，但总归还是会有一定损失。另外，所有属于gc root的GCPtr会加入一张哈希集合（root set），这也是一个主要性能影响点。实验数据表示，使用GCPtr一般会对应用程序性能造成至少30%左右的性能下降，因此不建议将GCPtr在性能严苛的场景里应用。

## 其它注意点
1. 请为被GCPtr管理的类的所有成员变量赋初始值，尤其是数组类型的成员变量，清零或其它初值皆可，如下所示。指针类型的成员变量也请赋为nullptr或进行相应的new操作（但无需对new出来的内存空间赋初值）。这是由于GC线程根据魔法值判定是否为GCPtr，如果你不想这么做，请启用GCParameter::fillZeroForNewRegion，以避免潜在的崩溃风险。（Linux/macOS下发生问题的概率较大，Windows下较小）。
```cpp
class YourObject {
public:
	int a[128];
	int* b;
	double c;
	int d;
	
	YourObject() : c(0), d(0) {			// 初始化成员变量为零
		memset(a, 0, sizeof(a));		// 初始化数组a为清零
		b = new int[128];				// 指针类型的b也请赋初值（new或nullptr），但无需对其指向的空间赋初值
		// b = nullptr;
	}
	
	YourObject(const YourObject& other) : c(other.c), d(other.d) {
		memcpy(a, other.a, sizeof(a));	// 拷贝构造直接复制数组
		// memset(a, 0, sizeof(a));		// 如果你不需要复制数组，也记得清零
		memcpy(b, other.b, 128 * sizeof(int));
	}
};
```

2. GCPtr目前不支持直接管理数组结构。请考虑使用std::vector或类似数据结构完成需求。

3. 不要从GCPtr取出裸指针。万一在裸指针使用的过程中此对象被转移，将导致不可预测的崩溃风险。如果你的确需要使用裸指针，请先取出其PtrGuard，再从PtrGuard中取出裸指针，并保证PtrGuard的生命周期覆盖裸指针使用的生命周期，如下所示：
```cpp
GCPtr<MyObject> myObj = gc::make_gc<MyObject>();
{
	PtrGuard<MyObject> ptr_guard = myObj.get();
	MyObject* raw_ptr = ptr_guard.get();
	// 针对raw_ptr的处理逻辑
	// 务必确保ptr_guard的生命周期覆盖raw_ptr的使用生命周期
}
```
PtrGuard会保证其存在期间指向的对象所在region不会被重定位，从而避免此风险。
<br/>

## 参数解释
GCPtr支持调整参数。这些参数在`GCParameter.h`中，并具有相应的解释。若不确定或有疑问可加末尾的群咨询。这里展示部分重要参数。

**enableConcurrentGC**：是否启用GC线程。若禁用该选项，GC过程将直接由应用线程进行。建议启用。

**enableMemoryAllocator**：是否启用内存分配器。若禁用该选项，所有内存分配会直接重定向到操作系统提供的new和malloc。该选项是启用移动式回收及正确判定gc root的前提，建议启用。

**enableRelocation**：是否启用内存碎片整理，即包含对象重分配的移动式回收。若禁用该选项，对象将不会被重定位。建议启用。如果你明确不需要内存碎片整理功能，可以禁用，但请注意这会导致一个region内只要有一个存活对象该region就不会被清空、因而造成内存浪费。（这里可以优化，如果你有需求可以加群提出）。

**enableParallelGC**：是否启用多线程垃圾回收。GC线程会开启线程池，对耗时的GC任务进行多线程优化。建议启用。

**enableDestructorSupport**：是否在回收对象时调用其析构函数。如果你的被GCPtr管理的类含有裸指针或STL的智能指针、需要在析构函数中手动析构的，你必须启用该选项以防止内存泄漏。否则，建议禁用该选项，因为调用析构函数会造成一定程度的性能损失。

**useRegionalHashMap**：对于GC采用的对象标记状态，是使用位图还是哈希表，默认为禁用即采用位图，启用则采用哈希表。两种数据结构各有优劣，位图天生线程安全，对于线程竞争激烈的情况较有优势，但其较为占内存，大小和堆大小成正比；哈希表则内存占用较小，大小和对象数量成正比，但不具备线程安全性需要加锁，并且计算哈希也需要消耗一定的CPU。这里建议用户对于启用和禁用都试一下，选择较高性能的一种。

**useInlineMarkState**：是否在GCPtr中记录对象标记状态。这个内联标记状态通常用于判定是否需要指针自愈用、以及跳过已标记的对象用。若你启用对象重定位，则必须启用该选项。

**useSecondaryMemoryManager**：是否启用二级内存池。若禁用该选项，每一块新region的分配直接从系统malloc而来；若启用该选项，则从此内存池为新region分配内存。启用该选项可以避免频繁向系统malloc，重利用已分配内存，能提高一定的内存分配性能（大约10-15%）。但目前实现尚未支持释放预留内存。

**enableMoveConstructor**：是否在重分配对象时调用其移动构造函数。若启用，当一个对象被重新分配到其它region时，会通过调用该对象的移动构造函数而不是直接memcpy（参考std::vector的扩容过程）。不要启用该选项，目前的实现不支持循环引用。如果你有需求请加文末的群。

**useConcurrentLinkedList**：是否使用无锁链表管理region，否则将使用std::deque。启用无锁链表对于添加删除region效率较高，但无法支持多线程回收。不建议启用该选项。

**deferRemoveRoot**：当一个GCPtr析构时，是否延迟删除其在root set。启用该选项可以提高GCPtr析构时的性能，但会增加root set内存占用。默认禁用。

**suspendThreadsWhenSTW**：是否在STW期间（重标记和选择转移集合两个阶段）暂停用户线程。若禁用，则会使用读写锁并仅阻塞针对GCPtr的操作。该选项仅支持Windows。默认禁用，不建议启用因为没有必要，但如果你遇上问题可以启用试一下。

**enableHashPool**：是否启用对线程id进行哈希后取模的池化方案。该选项会在多个地方起作用，例如分配新region、内存池等。若启用，则会对每次访问线程共享的变量时根据线程id，尽量分散开来缓解线程竞争。建议启用，但如果你的应用线程是单线程的话可以禁用。

**immediateClear**：尽量在一轮回收后就清除掉已经是垃圾的对象，否则将在2～3轮后再回收。启用此选项会增加垃圾回收的性能开销，但可以更快腾出内存。

**doNotRelocatePtrGuard**：禁止对任何已存在PtrGuard引用的region进行转移。若禁用，则会自旋等待直到PtrGuard析构。如果你的代码中存在相当长生命周期的PtrGuard建议启用该选项。

**zeroCountCondition**：如果当前被转移的region含有PtrGuard指向它，GC线程会休眠直到所有PtrGuard析构，否则GC线程会自旋等待。若PtrGuard较多时可以减少GC线程自旋消耗CPU，但会增加每次构造PtrGuard包括取出指针时的性能消耗（因为需要通过条件变量进行线程通信）。默认禁用。

**enablePtrRWLock**：针对GCPtr的若干个变量，使用读写锁保证其线程安全，启用该选项可以让GCPtr变得线程安全，无此需求请禁用。建议禁用。

**fillZeroForNewRegion**：为所有新region的内存清零填充。启用该选项可以解决因用户线程未对类成员变量初始化而引起的崩溃。默认禁用。

**waitingForGCFinished**：应用线程会等待GC线程完成所有工作再继续，也就是真正意义上完全Stop-the-World的垃圾回收。只有当你的程序遇上问题时可以启用该选项进行debug，否则请禁用。

**bitmapMemoryFromSecondary**：位图所使用的内存空间也来自二级内存池。启用该选项可以加快位图的内存分配，但可能导致二级内存池的碎片。

**TINY_OBJECT_THRESHOLD**：迷你对象的对象大小（上限）。默认24字节。

**TINY_REGION_SIZE**：迷你对象的region的大小。默认256KB。

**SMALL_OBJECT_THRESHOLD**：小对象的对象大小（上限）。默认16KB。

**SMALL_REGION_SIZE**：小对象的region的大小。默认2MB。

**MEDIUM_OBJECT_THRESHOLD**：中对象的对象大小（上限）。默认1MB。

**MEDIUM_REGION_SIZE**：中对象的region的大小。默认32MB。

**secondaryMallocSize**：二级内存池单次向操作系统请求预留的内存大小。默认8MB。

**evacuateFragmentRatio和evacuateFreeRatio**：当某region的碎片占比大于evacuateFragmentRatio且空余空间小于evacuateFreeRatio时，该region会被判定需要转移。默认为四分之一（0.25）。

***其余没展示的参数保持默认即可***

---
### 欢迎加QQ群820683439讨论

<!--stackedit_data:
eyJoaXN0b3J5IjpbNDA4NzY4NjAzLDM1MDY5NjQ5OCw1OTMyMz
EyMiwtMTIxODA2MDUyMCwtOTQ1NzQ0MTEwLC0yNTg3ODA4NTQs
MTU3NDg5MzU3NSwyNjA0MzA3NzUsMTA4MzMyNDcwOCwxNzExMT
kzNjAxLC0yMzQxMTE5MzEsMTk2MzA1MzQzNiwtODA2MDc4MDM5
LDQ3NTU5NTUxNiwxMzc3MDgyOTgzLC0xNDQzNjU4ODExLC0xMj
czMjQzNDEzLC0xMzI0NjkwMTgzLC0zOTE1NDE5NzQsLTEyODUy
OTE2MzldfQ==
-->