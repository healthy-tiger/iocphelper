#include <mempool.h>

size_t __alignedsize__(size_t size, size_t align)
{
    size_t n = size / align;
    if (size % align > 0)
    {
        n++;
    }
    return n * align;
}

MemPool *MemPoolInit(ULONG nEntries, size_t entrySize)
{
    size_t lstsize = __alignedsize__(sizeof(MemPool), MEMORY_ALLOCATION_ALIGNMENT);
    size_t esize = __alignedsize__(sizeof(MemEntry) + (entrySize - 1), MEMORY_ALLOCATION_ALIGNMENT);
    MemPool *pl;
    if (nEntries <= 0)
    {
        return NULL;
    }
    pl = (MemPool *)_aligned_malloc(lstsize + nEntries * esize, MEMORY_ALLOCATION_ALIGNMENT);
    InitializeSListHead((PSLIST_HEADER)pl);
    pl->nEntries = nEntries;
    pl->entrySize = esize;
    for (ULONG i = 0; i < nEntries; i++)
    {
        MemEntry *e = (MemEntry *)((char *)pl->entries + esize * i);
        e->used = FALSE;
        e->id = i;
        InterlockedPushEntrySList((PSLIST_HEADER)pl, (PSLIST_ENTRY)e);
    }
    return pl;
}

BOOL MemPoolDispose(MemPool *pl)
{
    if (pl == NULL)
    {
        return FALSE;
    }
    InterlockedFlushSList((PSLIST_HEADER)pl);
    _aligned_free(pl);
    return TRUE;
}

PVOID MemPoolAlloc(MemPool *pl, PULONG pid)
{
    if (pl == NULL || pid == NULL)
    {
        return NULL;
    }
    MemEntry *e = (MemEntry *)InterlockedPopEntrySList((PSLIST_HEADER)pl);
    if (e == NULL)
    {
        return NULL;
    }
    e->used = TRUE;
    *pid = e->id;
    return (PVOID)(e->data);
}

PVOID MemPoolGet(MemPool *pl, ULONG id) {
    if(pl == NULL) {
        return NULL;
    }
    if (id >= pl->nEntries)
    {
        return NULL;
    }
    MemEntry *e = (MemEntry *)((char *)pl->entries + pl->entrySize * id);
    if(e->used == FALSE) {
        return NULL;
    }
    return (PVOID)(e->data);
}

BOOL MemPoolFree(MemPool *pl, ULONG id)
{
    if (pl == NULL)
    {
        return FALSE;
    }
    if (id >= pl->nEntries)
    {
        return FALSE;
    }
    MemEntry *e = (MemEntry *)((char *)pl->entries + pl->entrySize * id);
    if(e->used == FALSE) {
        return FALSE;
    }
    e->used = FALSE;
    InterlockedPushEntrySList((PSLIST_HEADER)pl, (PSLIST_ENTRY)e);
    return TRUE;
}
