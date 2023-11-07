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

1. 准备阶段<br/>
准备阶段GC线程会做一些前置工作，例如重置gc数据、翻转当前标记状态等。此阶段不会耗费多少时间，不会阻塞应用线程。

2. 初始标记阶段<br/>
在初始标记阶段，GC线程会对所有的gc root中的GCPtr进行标记，代表所有gc root是存活的，并且后续的可达性分析也将在root上进行扫描。这个阶段会阻塞住应用线程所有针对gc root的操作（例如，创建GCPtr局部变量），但其它操作不受影响。<br/>
通常来说，gc root包含局部变量、全局变量和静态变量。不过，由于C++的特性所致，为了让GCPtr能够和裸指针共存，所有不在被GCPtr所管理的内存区域里的对象都将视为gc root并永远存活（除非它自己析构了）。

3. 并发标记阶段<br/>
在并发标记阶段，GC线程会在已标记的gc root上继续对所有被引用的对象进行标记；这个标记将采用深度优先搜索进行，具体来说，GC线程会扫描当前对象的所有内存的值，按照内存对齐原则进行遍历（64位就是8字节，32位就是4字节）。GCPtr会占用额外的8字节存储其标识符（魔法值，对象头0x1f1e33fc、对象尾0x3e0e1cc），如果在这个对象上找到了这两个魔法值，则会识别出其所指向的对象，并继续递归进行此轮扫描，直到所有对象都被标记了。<br/>
在GCPtr中，会用Remapped、M0和M1三种状态表示一个对象的标记状态，其中M0和M1表示被标记，两种状态交替使用；Remapped则代表非GC期间产生的新对象或被转移的对象，因此存活的Remapped对象理应在GC期间被置为M0/M1。<br/>
由于并发标记阶段与应用线程并行运行、不阻塞应用线程，因此可能会在标记的过程中发生引用变更。为了保证标记的正确性并不漏标，GC线程会采用基于删除屏障的三色标记策略，即“原始快照”（Snapshot-at-the-beginning, SATB）。当发生了删除行为（例如，显式地将某GCPtr置为nullptr，或某GCPtr析构）时，被删除的对象会被加入SATB队列，并在后续的重标记阶段重新标记。

4. 重标记阶段<br/>
在重标记阶段，会对并发标记阶段中因触发删除屏障而进入SATB队列中的对象进行重新扫描与标记。经过了重标记阶段后，所有存活对象都将被正确标记。这个过程将会阻塞应用线程。（准确的说是阻塞所有针对GCPtr的操作）。<br/>
另外，GC过程中新产生的对象将一律视为存活，即便该对象很快就死亡也需要在下一轮GC中才被回收（也就是所谓的浮动垃圾）。

5. 选择转移集合阶段<br/>
在选择转移集合阶段，GC线程将扫描所有的被管理内存区域。这个“被管理内存区域”代表所有调用gc::make_gc<>()分配给GCPtr所指向对象的内存区域。每一片内存区域按照对象大小，区分迷你、小、中、大三种内存区域（以下称为region）。小region默认为2MB一片，存放大小24字节～16KB的对象；中region默认为32MB一片，存放大小16KB～1MB的对象；大region则存放所有大于1MB的对象，每个对象独享一块region；迷你region默认为256KB一片，存放大小小于24字节的对象。<br/>
当调用gc::make_gc创建GCPtr时，将根据对象大小从相应种类的region中分配内存。分配按照指针碰撞法，也就是从当前已分配的偏移量开始，分配给新对象。因此，如果一个region中已分配区域中的对象已死亡的话，将会产生内存碎片。如果没有region有符合要求的空余空间，将分配新region。<br/>
选择转移集合将遍历所有region，根据一定条件判断此region的碎片比率和空闲比率。具体地说，当一片region中内存碎片量超过1/4，并且剩余空间小于1/4时，将会触发转移，也就是说，这个region中的所有对象将会被统统重新分配至其它region中，以实现内存碎片整理（也就是内存压缩）。转移具体会在并发转移阶段进行，这个阶段只会进行挑选要对哪些region进行转移。<br/>

6. 并发转移阶段<br/>
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

7. 收尾阶段<br/>
这个阶段GC线程会执行一些GC完成后的收尾工作，包括重置每个region的存活对象计数，清空临时变量等。此阶段不会耗费多少时间，不会阻塞应用线程。当此阶段结束后，一轮GC也就走完了。

## 常见Q&A
1. Q: 能否通过手动new一个对象，并把指针交给GCPtr管理以启用垃圾回收？<br/>
A: 否。所有被GCPtr<>管理的对象必须通过gc::make_gc<>()创建，不支持直接从对象指针构造GCPtr。但是你可以从一个已有的GCPtr构造新的GCPtr（也就GCPtr的拷贝构造）。
2. Q: GCPtr的稳定性如何？能否应用于生产环境？<br/>
A: 不建议将GCPtr应用于生产环境。此项目现在仍在开发与测试阶段，有许多不稳定和不完善的地方。欢迎加群或开issue提出问题。群号见本ReadMe末尾。
3. Q: GCPtr的性能如何？使用GCPtr是否会影响应用线程的运行速度？<br/>
A: 这个问题需要分情况讨论。通常，对于垃圾回收的性能谴责点一般在于其会造成Stop-the-World，也就是造成全部应用线程停顿。不过，在GCPtr中，所有需要阻塞应用线程的地方仅仅针对GCPtr的操作（构造、析构GCPtr），而且只有在初始标记、重标记、选择转移集合三个阶段才会阻塞。其余情况都完全不需要阻塞。实践也证明，即便是非常变态的实验数据（不断构造、析构GCPtr的测试用例），最长阻塞时间也不会超过5ms，绝大多数情况在1ms以内。<br/>
另一个主要性能影响点是删除屏障和读屏障造成的。删除屏障只会在并发标记阶段起作用，因此一般影响不大（但如果并发标记过程很长导致删除屏障频繁触发也会有点影响）。读屏障则会一直起作用，尤其是当完成一轮GC后的指针更新，尽管有根据标记状态判断是否需要更新的策略，但总归还是会有一定损失。另外，所有属于gc root的GCPtr会加入一张哈希集合（root set），这也是一个主要性能影响点。实验数据表示，使用GCPtr一般会对应用程序性能造成至少30%左右的性能下降，因此不建议将GCPtr在性能严苛的场景里应用。

## 其它注意点
1. 务必为被GCPtr管理的类的所有成员变量赋初始值，尤其是数组类型的成员变量，清零或其它初值皆可，如下所示。指针类型的成员变量也请赋为nullptr或进行相应的new操作（但无需对new出来的内存空间赋初值）。
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
	}
	
	YourObject(const YourObject& other) : c(other.c), d(other.d) {
		memcpy(a, other.a, sizeof(a));	// 拷贝构造直接复制数组
		// memset(a, 0, sizeof(a));		// 如果你不需要复制数组，也记得清零
		memcpy(b, other.b, 128 * sizeof(int));
	}
};
```
2. GCPtr目前不支持直接管理数组结构。请考虑使用std::vector或类似数据结构完成需求。
3. 不推荐从GCPtr取出裸指针。万一在裸指针使用的过程中此对象被转移，将导致不可预测的崩溃风险。如果你的确需要使用裸指针，请先取出其PtrGuard，再从PtrGuard中取出裸指针，并保证PtrGuard的生命周期覆盖裸指针使用的生命周期，如下所示：
```cpp
GCPtr<MyObject> myObj = gc::make_gc<MyObject>();
{
	PtrGuard<MyObject> ptr_guard = myObj.get();
	{
		MyObject* raw_ptr = ptr_guard.get();
		// 针对raw_ptr的处理逻辑
		// 请务必确保ptr_guard的生命周期覆盖raw_ptr的使用生命周期
	}
}
```
PtrGuard会保证其存在期间指向的对象不会被重定位，从而避免此风险。

<br/>

**欢迎加QQ群820683439讨论**

<!--stackedit_data:
eyJoaXN0b3J5IjpbMjYwNDMwNzc1LDEwODMzMjQ3MDgsMTcxMT
E5MzYwMSwtMjM0MTExOTMxLDE5NjMwNTM0MzYsLTgwNjA3ODAz
OSw0NzU1OTU1MTYsMTM3NzA4Mjk4MywtMTQ0MzY1ODgxMSwtMT
I3MzI0MzQxMywtMTMyNDY5MDE4MywtMzkxNTQxOTc0LC0xMjg1
MjkxNjM5LDEwMDQxMjMwNjcsMTQ5MTMxNjM0OCwtOTc0NTg3OS
wxODUyNTM1NTA4LDE0OTIxMzQ0NzQsNDcwNzczNDIzLC02MDY2
NjQ3NjFdfQ==
-->