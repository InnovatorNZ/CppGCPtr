#pragma once

#include <iostream>
#include <vector>
#include <Windows.h>
#include <TlHelp32.h>
#include "GCPhase.h"

bool is_stack_pointer(void* ptr) {
#if _WIN32
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high); // 获取当前线程的栈区边界
    return low <= reinterpret_cast<ULONG_PTR>(ptr) && reinterpret_cast<ULONG_PTR>(ptr) < high; // 判断指针是否在栈区范围内
#else
    // TODO: POSIX is_stack_pointer()
#endif
}

#if _WIN32

void print_user_threads() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot) {
        THREADENTRY32 threadEntry;
        threadEntry.dwSize = sizeof(threadEntry);
        if (Thread32First(hSnapshot, &threadEntry)) {
            do {
                if (threadEntry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(threadEntry.th32OwnerProcessID)) {
                    if (threadEntry.th32OwnerProcessID == GetCurrentProcessId() && threadEntry.th32ThreadID != GetCurrentThreadId()) {
                        HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadEntry.th32ThreadID);
                        if (hThread) {
                            DWORD dwExitCode = 0;
                            if (GetExitCodeThread(hThread, &dwExitCode) && dwExitCode == STILL_ACTIVE) {
                                DWORD status = SuspendThread(hThread);
                                if (status != -1) {
                                    printf("Thread ID: 0x%x suspended\n", threadEntry.th32ThreadID);
                                    Sleep(400);
                                    ResumeThread(hThread);
                                    printf("Thread ID: 0x%x resumed\n", threadEntry.th32ThreadID);
                                }
                                else {
                                    LPVOID lpMsgBuf;
                                    DWORD dw = GetLastError();
                                    FormatMessage(
                                        FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                        FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                        NULL,
                                        dw,
                                        MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
                                        (LPTSTR)&lpMsgBuf,
                                        0, NULL);
                                    printf("Thread ID: 0x%x suspend failure, error message: %ws", threadEntry.th32ThreadID, (LPCTSTR)lpMsgBuf);
                                }
                            }
                            CloseHandle(hThread);
                        }
                    }
                }
                threadEntry.dwSize = sizeof(threadEntry);
            } while (Thread32Next(hSnapshot, &threadEntry));
        }
        CloseHandle(hSnapshot);
    }
}

void suspend_user_threads(std::vector<DWORD>& suspendedThreadIDs) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot) {
        THREADENTRY32 threadEntry;
        threadEntry.dwSize = sizeof(threadEntry);
        if (Thread32First(hSnapshot, &threadEntry)) {
            do {
                if (threadEntry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(threadEntry.th32OwnerProcessID)) {
                    if (threadEntry.th32OwnerProcessID == GetCurrentProcessId() && threadEntry.th32ThreadID != GetCurrentThreadId()) {
                        HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadEntry.th32ThreadID);
                        if (hThread) {
                            DWORD dwExitCode = 0;
                            if (GetExitCodeThread(hThread, &dwExitCode) && dwExitCode == STILL_ACTIVE) {
                                DWORD status = SuspendThread(hThread);
                                if (status != -1) {
                                    std::clog << "Thread ID: 0x" << std::hex << threadEntry.th32ThreadID << "suspended" << std::endl;
                                    suspendedThreadIDs.push_back(threadEntry.th32ThreadID);
                                }
                                else {
                                    LPVOID lpMsgBuf;
                                    DWORD dw = GetLastError();
                                    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                        dw, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
                                    printf("Error: Thread ID: 0x%x suspend failure, error message: %ws", threadEntry.th32ThreadID, (LPCTSTR)lpMsgBuf);
                                }
                            }
                            CloseHandle(hThread);
                        }
                    }
                }
                threadEntry.dwSize = sizeof(threadEntry);
            } while (Thread32Next(hSnapshot, &threadEntry));
        }
        CloseHandle(hSnapshot);
    }
}

void resume_user_threads(const std::vector<DWORD>& suspendedThreadIDs) {
    for (const DWORD& threadID : suspendedThreadIDs) {
        HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadID);
        if (hThread) {
            DWORD status = ResumeThread(hThread);
            if (status != -1) {
                std::clog << "Thread ID: 0x" << std::hex << threadID << " resumed" << std::endl;
            }
            else {
                LPVOID lpMsgBuf;
                DWORD dw = GetLastError();
                FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                    dw, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
                printf("Error: Thread ID: 0x%x resume failure, error message: %ws", threadID, (LPCTSTR)lpMsgBuf);
            }
        }
    }
}

#else

// TODO: POSIX suspend_user_threads & resume_user_threads

#endif

class GCUtil {
private:
    static std::vector<DWORD> suspendedThreadIDs;
public:
    static void stop_the_world() {
        while (!GCPhase::notAllocating()) {
            std::clog << "Allocating object, waiting..." << std::endl;
            Sleep(1);
        }
        suspend_user_threads(suspendedThreadIDs);
    }

    static void resume_the_world() {
        resume_user_threads(suspendedThreadIDs);
        suspendedThreadIDs.clear();
    }
};

std::vector<DWORD> GCUtil::suspendedThreadIDs;
