#pragma once

class GCParameter {
public:
    static constexpr bool enableConcurrentGC = true;            // 并发GC，即是否启用GC线程
    static constexpr bool enableMemoryAllocator = true;         // 是否启用内存分配器；若否，所有内存分配使用内置new
    static constexpr bool enableRelocation = false;              // 是否启用含内存压缩的移动式回收；前提条件：启用内存分配器，启用内联标记
    static constexpr bool enableParallelGC = true;              // 是否启用多线程回收；前提条件：启用内存分配器
    static constexpr bool enableDestructorSupport = true;       // 是否在销毁对象时调用其析构函数
    static constexpr bool useRegionalHashmap = false;           // 是否使用局部哈希表而不是位图进行对象标记；前提条件：启用内存分配器
    static constexpr bool useInlineMarkState = false;            // 是否启用内联标记
    static constexpr bool useSecondaryMemoryManager = false;    // 是否启用二级内存分配器（尚未支持）
    static constexpr bool enableReclaim = false;                // 是否重利用内存（尚未支持）
};