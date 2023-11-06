#pragma once

#include <iostream>
#include <vector>
#include <thread>
#include <stdexcept>
#include "IReadWriteLock.h"
#include "GCParameter.h"
#include "CppExecutor/ThreadPoolExecutor.h"
#if _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#else
#include <unistd.h>
#endif

#if !_WIN32
typedef unsigned long       DWORD;
#endif

class GCUtil {
private:
    static std::vector<DWORD> _suspendedThreadIDs;

    static void suspend_user_threads(std::vector<DWORD>&, ThreadPoolExecutor*);

    static void resume_user_threads(const std::vector<DWORD>&);

    static bool user_threads_suspended;

public:
    static void stop_the_world(IReadWriteLock*, ThreadPoolExecutor* gcPool = nullptr,
                               bool suspend_user_thread = true);

    static void resume_the_world(IReadWriteLock* = nullptr);

    static bool is_stack_pointer(void* ptr);

    static int getPoolIdx(int poolCount);

    static void sleep(float sec);
};