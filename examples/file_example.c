#include <stdio.h>
#include <iocphelper.h>

#define READ_SIZE (128)

struct ReadInfo
{
    HANDLE hFile;
    const char *filename;
    char buf[READ_SIZE];
};

BOOL WINAPI OnCloseHandler(DWORD dwCtrlType);

void StartReadFile(IOCPHELPER hlpr, const char *filename);

void OnRead(IOCPHELPER hlp,
            DWORD status,
            PVOID user,
            DWORD dwBytesTransferred,
            LPOVERLAPPED overlapped);

HANDLE stopEvent;

int main(int argc, char *argv[])
{
    IOCPHELPER hlpr = IOCPHelperNew(10, 100);

    stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    SetConsoleCtrlHandler(OnCloseHandler, TRUE);

    for (int i = 1; i < argc; i++)
    {
        StartReadFile(hlpr, argv[i]);
    }

    WaitForSingleObject(stopEvent, INFINITE);

    IOCPHelperShutdown(hlpr, 1000);

    IOCPHelperDispose(hlpr);

    printf("Fin!\n");

    return 0;
}

BOOL WINAPI OnCloseHandler(DWORD dwCtrlType)
{
    SetEvent(stopEvent);
    return TRUE;
}

void OnRead(IOCPHELPER hlpr,
            DWORD status,
            PVOID user,
            DWORD dwBytesTransferred,
            LPOVERLAPPED overlapped)
{
    struct ReadInfo *ri = (struct ReadInfo *)user;

    if (status != ERROR_SUCCESS)
    {
        printf("%s: read error: %lu\n", ri->filename, status);
        goto closefile;
    }

    printf("%s: %lu bytes read\n", ri->filename, dwBytesTransferred);

    if (dwBytesTransferred < READ_SIZE)
    {
        goto closefile;
    }

    overlapped->Offset += dwBytesTransferred;
    BOOL bRet = ReadFile(ri->hFile, ri->buf, READ_SIZE, NULL, overlapped);
    if (bRet == FALSE)
    {
        if (ERROR_IO_PENDING != GetLastError())
        {
            fprintf(stderr, "%s: ReadFile() failed %lu\n", ri->filename, GetLastError());
            goto closefile;
        }
    }
    return;

closefile:
    CloseHandle(ri->hFile);
    free(ri);
    IOCPHelperReleaseCtx(hlpr, overlapped);
}

void StartReadFile(IOCPHELPER hlpr,  const char *filename)
{
    HANDLE hFile = CreateFile(filename, GENERIC_READ, 0,
                              NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        IOCPHelperRegisterHandle(hlpr, hFile);

        struct ReadInfo *ri = (struct ReadInfo *)malloc(sizeof(struct ReadInfo));
        ri->filename = filename;
        ri->hFile = hFile;

        LPOVERLAPPED overlapped = IOCPHelperNewCtx(hlpr, (PVOID)ri, OnRead);
        BOOL bRet = ReadFile(hFile, ri->buf, READ_SIZE, NULL, overlapped);
        if (bRet == FALSE)
        {
            if (ERROR_IO_PENDING != GetLastError())
            {
                fprintf(stderr, "ReadFile() failed: %lu\n", GetLastError());
                CloseHandle(hFile);
                IOCPHelperReleaseCtx(hlpr, overlapped);
            }
        }
    }
}
