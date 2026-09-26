#include "log.h"

static LOG_BUFFER* g_LogBuffer = NULL;

NTSTATUS LogInitialize(void)
{
    g_LogBuffer = (LOG_BUFFER*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(LOG_BUFFER), 'gLoH');
    if (!g_LogBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_LogBuffer, sizeof(LOG_BUFFER));
    HvLog("Log buffer initialized (%u entries)", LOG_BUFFER_CAPACITY);
    return STATUS_SUCCESS;
}

void LogDestroy(void)
{
    if (g_LogBuffer) {
        ExFreePoolWithTag(g_LogBuffer, 'gLoH');
        g_LogBuffer = NULL;
    }
}

void LogWrite(PLOG_ENTRY Entry)
{
    if (!g_LogBuffer)
        return;

    LONG slot = InterlockedIncrement(&g_LogBuffer->Head) % LOG_BUFFER_CAPACITY;
    RtlCopyMemory(&g_LogBuffer->Entries[slot], Entry, sizeof(LOG_ENTRY));
    InterlockedIncrement(&g_LogBuffer->Count);

    // Also print via DbgPrint for immediate visibility
    if (Entry->Type == LogRtlPcToFileHeader) {
        HvLog("RtlPcToFileHeader | PID=%u TID=%u RetAddr=0x%llX "
              "QueryPC=0x%llX ModuleBase=0x%llX",
              Entry->ProcessId, Entry->ThreadId, Entry->ReturnAddress,
              Entry->PcToFileHeader.PcAddress,
              Entry->PcToFileHeader.ResolvedBase);
    }
    else if (Entry->Type == LogMmGetSystemRoutineAddress) {
        HvLog("MmGetSystemRoutineAddress | PID=%u TID=%u RetAddr=0x%llX "
              "Func=\"%ws\" Resolved=0x%llX",
              Entry->ProcessId, Entry->ThreadId, Entry->ReturnAddress,
              Entry->GetRoutineAddress.FunctionName,
              Entry->GetRoutineAddress.ResolvedAddress);
    }
}
