#define WIN32_LEAN_AND_MEAN

#include <iocphelper.h>

#ifndef IOCPHELPER_MAX_HELPERS
#define IOCPHELPER_MAX_HELPERS (128)
#endif

#define TERMINATE_KEY_BASE (0x0000000000000000)
#define IOCPHELPER_KEY (0xabcd000000000000)
#define IOCPHELPER_KEY_MASK (0xffff000000000000)
#define IOCPHELPER_ID_MASK (0x0000ffffffffffff)

typedef struct tagIOCPHELPER
{
    HANDLE iocp;
    HANDLE heap;
    DWORD linger;
    int nWorkers;
    HANDLE workers[MAXIMUM_WAIT_OBJECTS];
} __IOCPHELPER;

DWORD WINAPI WorkerThreadProc(LPVOID lpParam);

typedef struct tagIHLP_OVERLAPPED
{
    OVERLAPPED overlapped;
    IOCPHELPER_COMPLETE_HANDLER complete;
    PVOID user;
} __OVERLAPPED;

INIT_ONCE helperHeapInit = INIT_ONCE_STATIC_INIT;
HANDLE helperHeap = NULL;

BOOL __initHelperHeap(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context)
{
    HANDLE heap = HeapCreate(0, 0,
                             sizeof(__IOCPHELPER) * IOCPHELPER_MAX_HELPERS);
    if (heap == NULL)
    {
        return FALSE;
    }
    helperHeap = heap;
    return TRUE;
}

IOCPHELPER *IOCPHELPER_API IOCPHelperNew(int nworkers, ULONG nMaxConcurrentTasks)
{
    if (nworkers <= 0 || nworkers > MAXIMUM_WAIT_OBJECTS || nMaxConcurrentTasks <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (FALSE == InitOnceExecuteOnce(&helperHeapInit, __initHelperHeap, NULL, NULL))
    {
        return NULL;
    }

    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                         NULL,
                                         (ULONG_PTR)NULL,
                                         nworkers);
    if (iocp == NULL)
    {
        return NULL;
    }

    HANDLE heap = HeapCreate(0, 0, nMaxConcurrentTasks * sizeof(__OVERLAPPED));
    if (heap == NULL)
    {
        CloseHandle(iocp);
        return NULL;
    }

    __IOCPHELPER *h = (__IOCPHELPER *)HeapAlloc(helperHeap,
                                                HEAP_ZERO_MEMORY,
                                                sizeof(__IOCPHELPER));
    if(h == NULL) {
        CloseHandle(iocp);
        HeapDestroy(heap);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    h->iocp = iocp;
    h->heap = heap;
    h->linger = INFINITE;
    h->nWorkers = nworkers;
    for (int i = 0; i < nworkers; i++)
    {
        h->workers[i] = CreateThread(NULL, 0, WorkerThreadProc, (LPVOID)h, 0, NULL);
    }

    return (IOCPHELPER *)h;
}

BOOL IOCPHELPER_API IOCPHelperRegisterHandle(IOCPHELPER _h, HANDLE iohandle)
{
    if (_h == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (iohandle == NULL || iohandle == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    __IOCPHELPER *h = (__IOCPHELPER *)_h;
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

BOOL IOCPHELPER_API IOCPHelperShutdown(IOCPHELPER _h, DWORD dwLinger)
{
    if (_h == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    if (dwLinger >= INFINITE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    __IOCPHELPER *h = (__IOCPHELPER *)_h;
    if (h->iocp == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    /* Terminate all worker threads */
    for (int i = 0; i < h->nWorkers; i++)
    {
        PostQueuedCompletionStatus(h->iocp, 0, (ULONG_PTR)(TERMINATE_KEY_BASE + i), NULL);
    }
    WaitForMultipleObjects(h->nWorkers,
                           h->workers,
                           TRUE,
                           INFINITE);
    /* Starts a cleaner thread to clean up unfinished IO. */
    if (dwLinger > 0)
    {
        h->linger = dwLinger;
        HANDLE cleaner = CreateThread(NULL, 0, WorkerThreadProc, (LPVOID)h, 0, NULL);
        WaitForSingleObject(cleaner, INFINITE);
        CloseHandle(cleaner);
    }
    return TRUE;
}

BOOL IOCPHELPER_API IOCPHelperDispose(IOCPHELPER _h)
{
    if (_h == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = (__IOCPHELPER *)_h;

    for (int i = 0; i < h->nWorkers; i++)
    {
        CloseHandle(h->workers[i]);
    }

    CloseHandle(h->iocp);
    HeapDestroy(h->heap);
    HeapFree(helperHeap, 0, h);
    return TRUE;
}

DWORD WINAPI WorkerThreadProc(LPVOID lpParam)
{
    __IOCPHELPER *h = (__IOCPHELPER *)lpParam;
    DWORD bt = 0;
    ULONG_PTR key = 0;
    __OVERLAPPED *ovl = NULL;
    DWORD status = ERROR_SUCCESS;

    while (TRUE)
    {
        bt = 0;
        key = 0;
        ovl = NULL;
        status = ERROR_SUCCESS;

        if (FALSE == GetQueuedCompletionStatus(h->iocp, &bt, &key, (LPOVERLAPPED *)&ovl, h->linger))
        {
            if (ovl == NULL)
            {
                break;
            }
            else
            {
                status = GetLastError();
            }
        }

        if ((key & IOCPHELPER_KEY_MASK) == IOCPHELPER_KEY)
        {
            ovl->complete(h, status, ovl->user, bt, (LPOVERLAPPED)ovl);
        }
        else if (key >= TERMINATE_KEY_BASE && key < TERMINATE_KEY_BASE + h->nWorkers)
        {
            break;
        }
    }

    return 0;
}

LPOVERLAPPED IOCPHELPER_API IOCPHelperNewCtx(IOCPHELPER _h, PVOID user, IOCPHELPER_COMPLETE_HANDLER complete)
{
    if (_h == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return NULL;
    }
    if (complete == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    __IOCPHELPER *h = (__IOCPHELPER *)_h;
    if (h->heap == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    __OVERLAPPED *ovl = (__OVERLAPPED *)HeapAlloc(h->heap, HEAP_ZERO_MEMORY, sizeof(__OVERLAPPED));
    if (ovl == NULL)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    ovl->user = user;
    ovl->complete = complete;

    return (LPOVERLAPPED)ovl;
}

BOOL IOCPHELPER_API IOCPHelperReleaseCtx(IOCPHELPER _h, LPOVERLAPPED ctx)
{
    if (_h == NULL || ctx == NULL)
    {
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    __IOCPHELPER *h = (__IOCPHELPER *)_h;
    if (h->heap == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    HeapFree(h->heap, 0, ctx);
    return TRUE;
}
