#pragma once
#include <ntddk.h>
#include "ept.h"

// Original function typedefs
typedef PVOID (NTAPI *FN_RtlPcToFileHeader)(
    PVOID PcValue,
    PVOID* BaseOfImage
);

typedef PVOID (NTAPI *FN_MmGetSystemRoutineAddress)(
    PUNICODE_STRING SystemRoutineName
);

// Globals holding pointers to original functions
extern FN_RtlPcToFileHeader    g_OrigRtlPcToFileHeader;
extern FN_MmGetSystemRoutineAddress g_OrigMmGetSystemRoutineAddress;

// Hook handlers (these are the detour functions patched into shadow pages)
PVOID NTAPI HkRtlPcToFileHeader(PVOID PcValue, PVOID* BaseOfImage);
PVOID NTAPI HkMmGetSystemRoutineAddress(PUNICODE_STRING SystemRoutineName);

// Setup
NTSTATUS HooksInstall(PEPT_STATE EptState);
