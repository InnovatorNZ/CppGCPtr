#pragma once

class GCParameter {
public:
	static constexpr bool enableConcurrentGC = true;			// 并发GC，即是否启用GC线程
	static constexpr bool enableMemoryAllocator = true;			// 是否启用内存分配器；若否，所有内存分配使用内置new
	static constexpr bool enableRelocation = true;				// 是否启用含内存压缩（对象重分配）的移动式回收；前提条件：启用内存分配器，启用内联标记
	static constexpr bool enableParallelGC = true;				// 是否启用多线程回收；前提条件：启用内存分配器
	static constexpr bool enableDestructorSupport = true;		// 是否在销毁对象时调用其析构函数
	static constexpr bool useRegionalHashmap = false;			// 是否使用局部哈希表而不是位图进行对象标记；前提条件：启用内存分配器
	static constexpr bool useInlineMarkState = true;			// 是否启用内联标记；当对象重分配启用时必须启用
	static constexpr bool useSecondaryMemoryManager = true;		// 是否启用二级内存分配池，若启用可以复用已分配内存以提升效率（目前尚未实现归还预留内存）；前提条件：启用内存分配器
	static constexpr bool enableReclaim = false;				// 是否重利用内存（尚未支持）
	static constexpr bool enableMoveConstructor = false;		// 是否在重分配对象时调用移动构造函数（不推荐，且不支持循环引用）；前提条件：启用重分配，启用析构函数，并且所有被GCPtr管理的对象是可移动的
	static constexpr bool enableRegionMapBuffer = false;		// 是否启用红黑树缓存（不推荐），若启用，将减小多线程竞争分配内存的锁粒度，但会增加root_set的内存占用；前提条件：启用移动构造函数
	static constexpr bool useConcurrentLinkedList = false;		// 是否使用无锁链表管理内存区域（不推荐，若启用会使多线程回收失效）
	static constexpr bool deferRemoveRoot = false;				// 是否延迟删除当作为gc root的GCPtr析构时，若启用会提升GCPtr析构时的性能，但会导致root set内存占用上升
	static constexpr bool suspendThreadsWhenSTW = false;		// 是否在STW期间暂停用户线程，若禁用则将仅使用读写锁阻塞；仅支持Windows
	static constexpr bool enableHashPool = true;				// 是否启用线程id进行hash后取模的池化方案；可以降低锁的竞争，但可能会产生计算哈希的开销
	static constexpr bool immediateClear = true;				// 尽量在一轮回收后就清除已是垃圾的对象，否则将在2~3轮后回收；启用此选项会增加垃圾回收的性能开销，但可以更快腾出内存
	static constexpr bool distinctSATB = false;					// 是否在对删除屏障引发的SATB去重；不推荐，因为没必要
	static constexpr bool useCopiedMarkstate = false;			// 是否引入无状态的内联标记（参见Solution 2.1 rev），可以解决当启用移动构造函数时的循环引用问题；不推荐，目前实现有问题，不要启用
	static constexpr bool doNotRelocatePtrGuard = false;		// 禁止对任何存在PtrGuard引用的region重分配，若禁用，则会自旋等待析构后再重分配；建议当存在相当长生命周期的PtrGuard时启用该选项；前提条件：启用重分配
	static constexpr bool enablePtrRWLock = false;				// 启用针对GCPtr的读写锁，启用该选项可以使GCPtr变得线程安全，无此需求请禁用
	static constexpr bool waitingForGCFinished = false;			// 完全Stop-the-world的GC，若遇上线程安全问题，可启用此选项进行debug，否则请禁用
	static constexpr bool zeroCountCondition = false;			// 当需要转移的region存在PtrGuard时，GC线程会休眠直到计数归零，在PtrGuard较多时可以减少GC线程的自旋消耗的CPU，但会增加应用线程每次取出指针的性能消耗
	static constexpr bool recordNewMemMap = false;				// 是否在分配新内存时记录其起始位置和大小，用于二级内存池释放预留内存用，没什么用，不建议启用；前提条件：启用二级内存分配器，启用释放预留内存
	static constexpr bool bitmapMemoryFromSecondary = true;		// 位图的内存是否从二级分配器分配；启用该选项可以加快位图的内存分配，但可能会导致二级分配器的内存碎片；前提条件：启用二级内存分配器
	static constexpr bool useGCPtrSet = false;					// 是否启用记录所有GCPtr的集合，若你的程序由于未对新分配内存进行初始化而在GC时崩溃可启用该选项，请谨慎这会导致较大的性能下降；前提条件：启用析构函数
	static constexpr size_t secondaryMallocSize = 8 * 1024 * 1024;		// 二级内存分配器单次向操作系统请求分配预留内存的大小（默认：8MB）
	static constexpr size_t TINY_OBJECT_THRESHOLD = 24;					// 迷你对象的对象大小上限（默认：24字节）
	static constexpr size_t TINY_REGION_SIZE = 256 * 1024;				// 迷你对象的区域大小（默认：256KB）
	static constexpr size_t SMALL_OBJECT_THRESHOLD = 16 * 1024;			// 小对象的对象大小上限（默认：16KB）
	static constexpr size_t SMALL_REGION_SIZE = 2 * 1024 * 1024;		// 小对象的区域大小（默认：2MB）
	static constexpr size_t MEDIUM_OBJECT_THRESHOLD = 1 * 1024 * 1024;	// 中对象的对象大小上限（默认：1MB）
	static constexpr size_t MEDIUM_REGION_SIZE = 32 * 1024 * 1024;		// 中对象的区域大小（默认：32MB）
};