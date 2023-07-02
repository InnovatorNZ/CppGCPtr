#pragma once

#include <iostream>
#include <vector>
#include <Windows.h>
#include <TlHelp32.h>
#include "IReadWriteLock.h"
#include "CppExecutor/ThreadPoolExecutor.h"

#if 0

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
                                } else {
                                    LPVOID lpMsgBuf;
                                    DWORD dw = GetLastError();
                                    FormatMessage(
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                            NULL,
                                            dw,
                                            MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
                                            (LPTSTR) &lpMsgBuf,
                                            0, NULL);
                                    printf("Thread ID: 0x%x suspend failure, error message: %ws", threadEntry.th32ThreadID, (LPCTSTR) lpMsgBuf);
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

#endif

class GCUtil {
private:
    static std::vector<DWORD> _suspendedThreadIDs;

    static void suspend_user_threads(std::vector<DWORD>&, ThreadPoolExecutor*);

    static void resume_user_threads(const std::vector<DWORD>&);

public:
    static void stop_the_world(IReadWriteLock*, ThreadPoolExecutor* gcPool = nullptr);

    static void resume_the_world();

    static bool is_stack_pointer(void* ptr);
};