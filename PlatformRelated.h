#ifndef CPPGCPTR_PLATFORMRELATED_H
#define CPPGCPTR_PLATFORMRELATED_H

#include <Windows.h>

bool is_stack_pointer(void* ptr) {
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high); // 获取当前线程的栈区边界
    return low <= reinterpret_cast<ULONG_PTR>(ptr) && reinterpret_cast<ULONG_PTR>(ptr) < high; // 判断指针是否在栈区范围内
}

#endif //CPPGCPTR_PLATFORMRELATED_H
