#ifndef __MEMPOOL_H__
#define __MEMPOOL_H__

#include <windows.h>
#include <stdalign.h>

typedef struct DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) tagMemEntry
{
    SLIST_ENTRY entry;
    BOOL used;
    ULONG id;
    char data[1];
} MemEntry;

typedef DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) struct tagMemPool
{
    SLIST_HEADER header;
    ULONG nEntries;
    size_t entrySize;
    MemEntry entries[1];
} MemPool;

MemPool *MemPoolInit(ULONG nEntries, size_t entrySize);
BOOL MemPoolDispose(MemPool *pool);
PVOID MemPoolAlloc(MemPool *pool, PULONG pid);
PVOID MemPoolGet(MemPool *pool, ULONG id);
BOOL MemPoolFree(MemPool *pool, ULONG id);

#endif