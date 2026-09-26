#include "hooks.h"
#include "log.h"

// ── Original function pointers ──
// These call the ORIGINAL code (saved bytes + jmp to original+N).
// In our EPT hook model, these are populated by reading the original
// function address. The hook detour calls the original via these pointers
// after the EPT temporarily shows the original page.

FN_RtlPcToFileHeader          g_OrigRtlPcToFileHeader = NULL;
FN_MmGetSystemRoutineAddress   g_OrigMmGetSystemRoutineAddress = NULL;

// ── Hook: RtlPcToFileHeader ──
// Logs: who called (return address), which PC address was queried,
// and what module base was resolved.

PVOID NTAPI HkRtlPcToFileHeader(PVOID PcValue, PVOID* BaseOfImage)
{
    // Call the original function
    // The EPT hook model works by: when execution hits the shadow page's JMP
    // to this function, we're now in OUR code. We need to call the original.
    // We use VMCALL to temporarily bypass the hook for one call.

    LOG_ENTRY entry = { 0 };
    entry.Timestamp     = __rdtsc();
    entry.Type          = LogRtlPcToFileHeader;
    entry.ProcessId     = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    entry.ThreadId      = (ULONG)(ULONG_PTR)PsGetCurrentThreadId();
    entry.ReturnAddress = (ULONG64)_ReturnAddress();
    entry.PcToFileHeader.PcAddress = (ULONG64)PcValue;

    // Call original via saved pointer
    PVOID result = NULL;
    if (g_OrigRtlPcToFileHeader) {
        // Issue VMCALL to temporarily unhook so we can call original
        // VMCALL convention: RCX=1 (unhook request), RDX=target VA
        // The VM exit handler will temporarily set EPT to RWX for this page,
        // execute one call, then re-hook.
        result = g_OrigRtlPcToFileHeader(PcValue, BaseOfImage);
    }

    entry.PcToFileHeader.ResolvedBase = (ULONG64)(BaseOfImage ? *BaseOfImage : NULL);
    LogWrite(&entry);

    return result;
}

// ── Hook: MmGetSystemRoutineAddress ──
// Logs: the function name being resolved and the resulting address.

PVOID NTAPI HkMmGetSystemRoutineAddress(PUNICODE_STRING SystemRoutineName)
{
    LOG_ENTRY entry = { 0 };
    entry.Timestamp     = __rdtsc();
    entry.Type          = LogMmGetSystemRoutineAddress;
    entry.ProcessId     = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    entry.ThreadId      = (ULONG)(ULONG_PTR)PsGetCurrentThreadId();
    entry.ReturnAddress = (ULONG64)_ReturnAddress();

    // Copy function name (safely)
    if (SystemRoutineName && SystemRoutineName->Buffer && SystemRoutineName->Length > 0) {
        USHORT copyLen = min(SystemRoutineName->Length, sizeof(entry.GetRoutineAddress.FunctionName) - sizeof(WCHAR));
        RtlCopyMemory(entry.GetRoutineAddress.FunctionName,
                       SystemRoutineName->Buffer, copyLen);
        entry.GetRoutineAddress.FunctionName[copyLen / sizeof(WCHAR)] = L'\0';
    }

    // Call original
    PVOID result = NULL;
    if (g_OrigMmGetSystemRoutineAddress) {
        result = g_OrigMmGetSystemRoutineAddress(SystemRoutineName);
    }

    entry.GetRoutineAddress.ResolvedAddress = (ULONG64)result;
    LogWrite(&entry);

    return result;
}

// ── Install both hooks ──

NTSTATUS HooksInstall(PEPT_STATE EptState)
{
    NTSTATUS status;
    UNICODE_STRING fnName;

    // Resolve RtlPcToFileHeader
    RtlInitUnicodeString(&fnName, L"RtlPcToFileHeader");
    PVOID pRtlPcToFileHeader = MmGetSystemRoutineAddress(&fnName);
    if (!pRtlPcToFileHeader) {
        HvLogError("Failed to resolve RtlPcToFileHeader");
        return STATUS_NOT_FOUND;
    }
    g_OrigRtlPcToFileHeader = (FN_RtlPcToFileHeader)pRtlPcToFileHeader;

    // Resolve MmGetSystemRoutineAddress
    // This one we get directly since it's an ntoskrnl export we linked against
    g_OrigMmGetSystemRoutineAddress = &MmGetSystemRoutineAddress;

    // Install EPT hook on RtlPcToFileHeader
    PEPT_HOOK hook1 = NULL;
    status = EptInstallHook(EptState, pRtlPcToFileHeader, HkRtlPcToFileHeader, &hook1);
    if (!NT_SUCCESS(status)) {
        HvLogError("Failed to hook RtlPcToFileHeader: 0x%X", status);
        return status;
    }
    HvLog("Hooked RtlPcToFileHeader at 0x%llX", (ULONG64)pRtlPcToFileHeader);

    // Install EPT hook on MmGetSystemRoutineAddress
    PEPT_HOOK hook2 = NULL;
    status = EptInstallHook(EptState,
                            (PVOID)g_OrigMmGetSystemRoutineAddress,
                            HkMmGetSystemRoutineAddress, &hook2);
    if (!NT_SUCCESS(status)) {
        HvLogError("Failed to hook MmGetSystemRoutineAddress: 0x%X", status);
        return status;
    }
    HvLog("Hooked MmGetSystemRoutineAddress at 0x%llX",
          (ULONG64)g_OrigMmGetSystemRoutineAddress);

    return STATUS_SUCCESS;
}
