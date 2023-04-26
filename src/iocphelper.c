#define WIN32_LEAN_AND_MEAN

#include <iocphelper.h>
#include <mempool.h>

#ifndef IOCPHELPER_MAX_HELPERS
#define IOCPHELPER_MAX_HELPERS (128)
#endif

#define TERMINATE_KEY_BASE (0x0000000000000000)
#define IOCPHELPER_KEY (0xabcd000000000000)
#define IOCPHELPER_KEY_MASK (0xffff000000000000)
#define IOCPHELPER_ID_MASK (0x0000ffffffffffff)

typedef struct tagIOCPHELPER
{
    ULONG id;
    HANDLE iocp;
    MemPool *taskPool;
    int nWorkers;
    HANDLE workers[1];
} __IOCPHELPER;

DWORD WINAPI WorkerThreadProc(LPVOID lpParam);

static MemPool *hlpPool = NULL;

typedef struct tagIHLP_OVERLAPPED
{
    OVERLAPPED overlapped;
    ULONG id;
    IOCPHLPR_COMPLETE_HANDLER complete;
    PVOID user;
} IHLP_OVERLAPPED, *PIHLP_OVERLAPPED;

BOOL IOCPHLPR_API IOCPHelperStartup(ULONG nMaxInstance)
{
    if (hlpPool == NULL)
    {
        ULONG max = nMaxInstance;
        if(max == 0) {
            max = IOCPHELPER_MAX_HELPERS;
        }
        MemPool *mp = MemPoolInit(max, sizeof(__IOCPHELPER *));
        if (mp == NULL)
        {
            return FALSE;
        }
        hlpPool = mp;
    }
    return TRUE;
}

void IOCPHLPR_API IOCPHelperShutdown()
{
    if (hlpPool == NULL)
    {
        return;
    }

    for (int i = 0; i < hlpPool->nEntries; i++)
    {
        __IOCPHELPER *hlp = (__IOCPHELPER *)MemPoolGet(hlpPool, i);
        if (hlp != NULL)
        {
            IOCPHLPR_Close(i);
            MemPoolFree(hlpPool, i);
        }
    }
    MemPoolDispose(hlpPool);
}

BOOL IOCPHLPR_API IOCPHLPR_New(int nworkers, ULONG nMaxConcurrentTasks, IOCPHLPR *phlpr)
{
    if (nworkers <= 0)
    {
        return FALSE;
    }
    if (hlpPool == NULL)
    {
        return FALSE;
    }

    __IOCPHELPER *h = NULL;
    size_t size;

    ULONG id;
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolAlloc(hlpPool, &id);
    if (ph == NULL)
    {
        return FALSE;
    }

    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                         NULL,
                                         (ULONG_PTR)NULL,
                                         nworkers);
    if (iocp == NULL)
    {
        MemPoolFree(hlpPool, id);
        return FALSE;
    }

    MemPool *taskpool = MemPoolInit(nMaxConcurrentTasks, sizeof(IHLP_OVERLAPPED));
    if (taskpool == NULL)
    {
        CloseHandle(iocp);
        MemPoolFree(hlpPool, id);
        return FALSE;
    }

    size = sizeof(__IOCPHELPER) + sizeof(HANDLE) * (nworkers - 1);
    h = (__IOCPHELPER *)malloc(size);
    ZeroMemory(h, size);
    h->id = id;
    h->iocp = iocp;
    h->taskPool = taskpool;
    h->nWorkers = nworkers;
    for (int i = 0; i < nworkers; i++)
    {
        h->workers[i] = CreateThread(NULL,
                                       0,
                                       WorkerThreadProc,
                                       (LPVOID)h,
                                       CREATE_SUSPENDED,
                                       NULL);
    }
    *ph = h;
    *phlpr = id;
    return TRUE;
}

BOOL IOCPHLPR_API IOCPHLPR_Start(IOCPHLPR hlpr)
{
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = *ph;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    for (int i = 0; i < h->nWorkers; i++)
    {
        ResumeThread(h->workers[i]);
    }
    return TRUE;
}

BOOL IOCPHLPR_API IOCPHLPR_Register(IOCPHLPR hlpr, HANDLE iohandle)
{
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = *ph;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HANDLE iocp = CreateIoCompletionPort(iohandle, h->iocp, (ULONG_PTR)IOCPHELPER_KEY, 0);
    if (iocp == NULL)
    {
        return FALSE;
    }
    return TRUE;
}

BOOL IOCPHLPR_API IOCPHLPR_Wait(IOCPHLPR hlpr, DWORD timeout)
{
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = *ph;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DWORD dwResult = WaitForMultipleObjects(h->nWorkers,
                                            h->workers,
                                            TRUE,
                                            timeout);
    if (dwResult < WAIT_OBJECT_0 || dwResult >= WAIT_OBJECT_0 + h->nWorkers)
    {
        return TRUE;
    }
    return FALSE;
}

BOOL IOCPHLPR_API IOCPHLPR_Stop(IOCPHLPR hlpr)
{
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = *ph;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    for (int i = 0; i < h->nWorkers; i++)
    {
        PostQueuedCompletionStatus(h->iocp, 0, (ULONG_PTR)(TERMINATE_KEY_BASE + i), NULL);
    }
    return TRUE;
}

BOOL IOCPHLPR_API IOCPHLPR_Close(IOCPHLPR hlpr)
{
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = *ph;
    for (int i = 0; i < h->nWorkers; i++)
    {
        CloseHandle(h->workers[i]);
    }
    // Call the completion handler for pending requests.
    for (int i = 0; i < h->taskPool->nEntries; i++)
    {
        PIHLP_OVERLAPPED ovl = MemPoolGet(h->taskPool, i);
        if (ovl != NULL)
        {
            ovl->complete(h->id, ERROR_SUCCESS, ovl->user, 0, (LPOVERLAPPED)ovl);
            MemPoolFree(h->taskPool, i);
        }
    }
    if (h->iocp != NULL)
    {
        CloseHandle(h->iocp);
    }
    MemPoolDispose(h->taskPool);
    MemPoolFree(hlpPool, hlpr);
    free(h);
    return TRUE;
}

DWORD WINAPI WorkerThreadProc(LPVOID lpParam)
{
    __IOCPHELPER *h = (__IOCPHELPER *)lpParam;
    DWORD bt;
    ULONG_PTR key;
    PIHLP_OVERLAPPED ovl;
    DWORD status;

    while (TRUE)
    {
        key = 0;
        ovl = NULL;
        status = ERROR_SUCCESS;
        bt = 0;
        if (FALSE == GetQueuedCompletionStatus(h->iocp, &bt, &key, (LPOVERLAPPED *)&ovl, INFINITE))
        {
            if (ovl == NULL)
            {
                continue;
            }
            else
            {
                status = GetLastError();
            }
        }

        if ((key & IOCPHELPER_KEY_MASK) == IOCPHELPER_KEY)
        {
            ovl->complete(h->id, status, ovl->user, bt, (LPOVERLAPPED)ovl);
        }
        else if (key >= TERMINATE_KEY_BASE && key < TERMINATE_KEY_BASE + h->nWorkers)
        {
            break;
        }
    }

    return 0;
}

LPOVERLAPPED IOCPHLPR_API IOCPHLPR_NewCtx(IOCPHLPR hlpr, PVOID user, IOCPHLPR_COMPLETE_HANDLER complete) {
    if(complete == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return NULL;
    }
    __IOCPHELPER *h = *ph;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    ULONG id;
    PIHLP_OVERLAPPED ovl = (PIHLP_OVERLAPPED)MemPoolAlloc(h->taskPool, &id);
    ZeroMemory(ovl, sizeof(IHLP_OVERLAPPED));
    if (ovl == NULL)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    ovl->id = id;
    ovl->user = user;
    ovl->complete = complete;

    return (LPOVERLAPPED)ovl;
}

BOOL IOCPHLPR_API IOCPHLPR_ReleaseCtx(IOCPHLPR hlpr, LPOVERLAPPED ctx) {
    if(ctx == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    __IOCPHELPER **ph = (__IOCPHELPER **)MemPoolGet(hlpPool, hlpr);
    if (ph == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = *ph;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    PIHLP_OVERLAPPED ovl = (PIHLP_OVERLAPPED)ctx;
    if(FALSE == MemPoolFree(h->taskPool, ovl->id)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}
