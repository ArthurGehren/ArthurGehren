#include "hooklog.h"

NTSTATUS HookLogInit(PHOOK_LOG_BUFFER *OutBuffer)
{
    PHOOK_LOG_BUFFER buf = (PHOOK_LOG_BUFFER)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(HOOK_LOG_BUFFER), HOOKLOG_TAG);
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(buf, sizeof(HOOK_LOG_BUFFER));
    KeInitializeSpinLock(&buf->ReadLock);
    *OutBuffer = buf;
    return STATUS_SUCCESS;
}

VOID HookLogDestroy(PHOOK_LOG_BUFFER Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, HOOKLOG_TAG);
}

VOID HookLogWrite(PHOOK_LOG_BUFFER Buffer, PHOOK_LOG_ENTRY Entry)
{
    LONG slot = InterlockedIncrement(&Buffer->Head) - 1;
    slot = slot % MAX_LOG_ENTRIES;
    if (slot < 0) slot += MAX_LOG_ENTRIES;

    RtlCopyMemory(&Buffer->Entries[slot], Entry, sizeof(HOOK_LOG_ENTRY));

    if (Entry->Type == HOOK_EVENT_RTLPC_TO_FILE_HEADER)
        InterlockedIncrement64(&Buffer->TotalRtlPc);
    else if (Entry->Type == HOOK_EVENT_MM_GET_SYSTEM_ROUTINE)
        InterlockedIncrement64(&Buffer->TotalMmGet);

    LONG count = InterlockedIncrement(&Buffer->Count);
    if (count > MAX_LOG_ENTRIES) {
        InterlockedCompareExchange(&Buffer->Count, MAX_LOG_ENTRIES, count);
        InterlockedIncrement(&Buffer->Dropped);
    }
}

ULONG HookLogRead(PHOOK_LOG_BUFFER Buffer, PHOOK_LOG_ENTRY OutEntries, ULONG MaxEntries)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&Buffer->ReadLock, &oldIrql);

    LONG count = InterlockedExchange(&Buffer->Count, 0);
    if (count <= 0) {
        KeReleaseSpinLock(&Buffer->ReadLock, oldIrql);
        return 0;
    }
    if (count > (LONG)MaxEntries)
        count = (LONG)MaxEntries;

    LONG head = Buffer->Head % MAX_LOG_ENTRIES;
    LONG start = head - count;
    if (start < 0) start += MAX_LOG_ENTRIES;

    for (LONG i = 0; i < count; i++) {
        LONG idx = (start + i) % MAX_LOG_ENTRIES;
        RtlCopyMemory(&OutEntries[i], &Buffer->Entries[idx], sizeof(HOOK_LOG_ENTRY));
    }

    KeReleaseSpinLock(&Buffer->ReadLock, oldIrql);
    return (ULONG)count;
}

VOID HookLogClear(PHOOK_LOG_BUFFER Buffer)
{
    InterlockedExchange(&Buffer->Count, 0);
    InterlockedExchange(&Buffer->Head, 0);
    InterlockedExchange64(&Buffer->TotalRtlPc, 0);
    InterlockedExchange64(&Buffer->TotalMmGet, 0);
    InterlockedExchange(&Buffer->Dropped, 0);
}

VOID HookLogGetStats(PHOOK_LOG_BUFFER Buffer, PHOOK_LOG_STATS Stats)
{
    Stats->TotalRtlPcToFileHeader = Buffer->TotalRtlPc;
    Stats->TotalMmGetSystemRoutine = Buffer->TotalMmGet;
    Stats->CurrentEntries = (UINT32)Buffer->Count;
    Stats->DroppedEntries = (UINT32)Buffer->Dropped;
}
