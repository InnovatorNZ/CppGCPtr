#pragma once

class GCParameter {
public:
	static constexpr bool enableConcurrentGC = true;			// 并发GC，即是否启用GC线程
	static constexpr bool enableMemoryAllocator = true;			// 是否启用内存分配器；若否，所有内存分配使用内置new
	static constexpr bool enableRelocation = true;				// 是否启用含内存压缩（对象重分配）的移动式回收；前提条件：启用内存分配器，启用内联标记
	static constexpr bool enableParallelGC = true;				// 是否启用多线程回收；前提条件：启用内存分配器
	static constexpr bool enableDestructorSupport = true;		// 是否在销毁对象时调用其析构函数
	static constexpr bool useRegionalHashmap = true;			// 是否使用局部哈希表而不是位图进行对象标记；前提条件：启用内存分配器
	static constexpr bool useInlineMarkState = true;			// 是否启用内联标记；当对象重分配启用时必须启用
	static constexpr bool useSecondaryMemoryManager = false;	// 是否启用二级内存分配器（尚未支持）
	static constexpr bool enableReclaim = false;				// 是否重利用内存（尚未支持）
	static constexpr bool enableMoveConstructor = false;		// 是否在重分配对象时调用移动构造函数（不推荐，有问题才启用）；前提条件：启用重分配，启用析构函数，并且所有被GCPtr管理的对象是可移动的
	static constexpr bool enableRegionMapBuffer = false;		// 是否启用红黑树缓存（不推荐），若启用，将减小多线程竞争分配内存的锁粒度，但会增加root_set的内存占用；前提条件：启用移动构造函数
	static constexpr bool useConcurrentLinkedList = false;		// 是否使用无锁链表管理内存区域（不推荐，若启用会使多线程回收失效）
	static constexpr bool deferRemoveRoot = false;				// 是否延迟删除当gc root的GCPtr析构时，若启用会提升GCPtr析构时的性能但会导致root set内存占用上升
};