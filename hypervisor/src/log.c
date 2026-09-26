#include "../include/log.h"

// ============================================================
// Ring-buffer de log lock-free para uso em VMX root
// ============================================================

NTSTATUS LogInit(PLOG_BUFFER *OutBuffer)
{
    PLOG_BUFFER buf = (PLOG_BUFFER)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(LOG_BUFFER), LOG_TAG);
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(buf, sizeof(LOG_BUFFER));
    KeInitializeSpinLock(&buf->Lock);
    *OutBuffer = buf;
    return STATUS_SUCCESS;
}

VOID LogDestroy(PLOG_BUFFER Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, LOG_TAG);
}

VOID LogWrite(PLOG_BUFFER Buffer, PLOG_ENTRY Entry)
{
    LONG slot = InterlockedIncrement(&Buffer->Head) - 1;
    slot = slot % MAX_LOG_ENTRIES;
    if (slot < 0)
        slot += MAX_LOG_ENTRIES;

    RtlCopyMemory(&Buffer->Entries[slot], Entry, sizeof(LOG_ENTRY));

    LONG count = InterlockedIncrement(&Buffer->Count);
    if (count > MAX_LOG_ENTRIES)
        InterlockedCompareExchange(&Buffer->Count, MAX_LOG_ENTRIES, count);
}

ULONG LogRead(PLOG_BUFFER Buffer, PLOG_ENTRY OutEntries, ULONG MaxEntries)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&Buffer->Lock, &oldIrql);

    LONG count = InterlockedExchange(&Buffer->Count, 0);
    if (count > (LONG)MaxEntries)
        count = (LONG)MaxEntries;
    if (count <= 0) {
        KeReleaseSpinLock(&Buffer->Lock, oldIrql);
        return 0;
    }

    LONG head = Buffer->Head % MAX_LOG_ENTRIES;
    LONG start = head - count;
    if (start < 0)
        start += MAX_LOG_ENTRIES;

    for (LONG i = 0; i < count; i++) {
        LONG idx = (start + i) % MAX_LOG_ENTRIES;
        RtlCopyMemory(&OutEntries[i], &Buffer->Entries[idx], sizeof(LOG_ENTRY));
    }

    KeReleaseSpinLock(&Buffer->Lock, oldIrql);
    return (ULONG)count;
}

VOID LogClear(PLOG_BUFFER Buffer)
{
    InterlockedExchange(&Buffer->Count, 0);
    InterlockedExchange(&Buffer->Head, 0);
}
