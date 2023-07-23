#pragma once

class GCParameter {
public:
    static constexpr bool enableConcurrentGC = true;
    static constexpr bool enableMemoryAllocator = true;
    static constexpr bool enableRelocation = true;
    static constexpr bool enableParallelGC = true;
    static constexpr bool enableDestructorSupport = false;
    static constexpr bool useInlineMarkState = true;
    static constexpr bool useSecondaryMemoryManager = false;
    static constexpr bool enableReclaim = false;
};