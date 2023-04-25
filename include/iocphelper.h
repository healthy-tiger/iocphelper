#ifndef __IOCPHELPER_H__
#define __IOCPHELPER_H__

#include <windows.h>

#ifdef BUILD_DLL
#define IOCPHLPR_API    __declspec( dllexport )
#else
#define IOCPHLPR_API    __declspec( dllimport )
#endif

#ifdef __cplusplus
extern "C" {
#endif

BOOL IOCPHLPR_API IOCPHelperStartup(ULONG nMaxInstance);
void IOCPHLPR_API IOCPHelperShutdown();

typedef ULONG IOCPHLPR;

typedef void (*IOCPHLPR_COMPLETE_HANDLER)(IOCPHLPR hlpr,
                                          DWORD status,
                                          PVOID user,
                                          DWORD dwBytesTransferred,
                                          LPOVERLAPPED overlapped);

BOOL IOCPHLPR_API IOCPHLPR_New(int nworkers, ULONG nMaxConcurrentTasks, IOCPHLPR *phlpr);
BOOL IOCPHLPR_API IOCPHLPR_Start(IOCPHLPR hlpr);
BOOL IOCPHLPR_API IOCPHLPR_Register(IOCPHLPR hlpr, HANDLE iohandle);
BOOL IOCPHLPR_API IOCPHLPR_Wait(IOCPHLPR hlpr, DWORD timeout);
BOOL IOCPHLPR_API IOCPHLPR_Stop(IOCPHLPR hlpr);
BOOL IOCPHLPR_API IOCPHLPR_Close(IOCPHLPR hlpr);

LPOVERLAPPED IOCPHLPR_API IOCPHLPR_NewCtx(IOCPHLPR hlpr, PVOID user, IOCPHLPR_COMPLETE_HANDLER complete);
BOOL IOCPHLPR_API IOCPHLPR_ReleaseCtx(IOCPHLPR hlpr, LPOVERLAPPED ctx);

#ifdef __cplusplus
}
#endif

#endif
