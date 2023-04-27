#ifndef __IOCPHELPER_H__
#define __IOCPHELPER_H__

#include <windows.h>

#ifdef BUILD_DLL
#define IOCPHELPER_API __declspec(dllexport)
#else
#define IOCPHELPER_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void *IOCPHELPER;

    typedef void (*IOCPHELPER_COMPLETE_HANDLER)(IOCPHELPER helper,
                                                DWORD status,
                                                PVOID user,
                                                DWORD dwBytesTransferred,
                                                LPOVERLAPPED overlapped);

    IOCPHELPER *IOCPHELPER_API IOCPHelperNew(int nworkers, ULONG nMaxConcurrentTasks);
    BOOL IOCPHELPER_API IOCPHelperRegisterHandle(IOCPHELPER helper, HANDLE iohandle);
    BOOL IOCPHELPER_API IOCPHelperShutdown(IOCPHELPER helper, DWORD linger);
    BOOL IOCPHELPER_API IOCPHelperDispose(IOCPHELPER helper);

    LPOVERLAPPED IOCPHELPER_API IOCPHelperNewCtx(IOCPHELPER helper, PVOID user, IOCPHELPER_COMPLETE_HANDLER complete);
    BOOL IOCPHELPER_API IOCPHelperReleaseCtx(IOCPHELPER helper, LPOVERLAPPED ctx);

#ifdef __cplusplus
}
#endif

#endif
