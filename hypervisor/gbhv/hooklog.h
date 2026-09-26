#pragma once
#include "extern.h"

// ============================================================
// Ring-buffer log compartilhado entre VMX root e user-mode
// ============================================================

#define HOOKLOG_TAG     'gLoH'
#define MAX_LOG_ENTRIES  4096
#define MAX_ROUTINE_NAME 64

#define IOCTL_HV_READ_LOG   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_HV_CLEAR_LOG  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_HV_GET_STATS  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef enum _HOOK_EVENT_TYPE {
    HOOK_EVENT_RTLPC_TO_FILE_HEADER     = 1,
    HOOK_EVENT_MM_GET_SYSTEM_ROUTINE    = 2,
} HOOK_EVENT_TYPE;

#pragma pack(push, 1)
typedef struct _HOOK_LOG_ENTRY {
    LARGE_INTEGER       Timestamp;
    HOOK_EVENT_TYPE     Type;
    UINT32              ProcessId;
    UINT32              ThreadId;
    UINT64              CallerRip;
    UINT64              ReturnAddress;
    union {
        struct {
            UINT64      PcAddress;
            UINT64      ResolvedBase;
        } RtlPcToFileHeader;
        struct {
            WCHAR       RoutineName[MAX_ROUTINE_NAME];
        } MmGetSystemRoutine;
    } Detail;
} HOOK_LOG_ENTRY, *PHOOK_LOG_ENTRY;

typedef struct _HOOK_LOG_STATS {
    UINT64  TotalRtlPcToFileHeader;
    UINT64  TotalMmGetSystemRoutine;
    UINT32  CurrentEntries;
    UINT32  DroppedEntries;
} HOOK_LOG_STATS, *PHOOK_LOG_STATS;
#pragma pack(pop)

typedef struct _HOOK_LOG_BUFFER {
    HOOK_LOG_ENTRY      Entries[MAX_LOG_ENTRIES];
    volatile LONG       Head;
    volatile LONG       Count;
    volatile LONG64     TotalRtlPc;
    volatile LONG64     TotalMmGet;
    volatile LONG       Dropped;
    KSPIN_LOCK          ReadLock;
} HOOK_LOG_BUFFER, *PHOOK_LOG_BUFFER;

NTSTATUS HookLogInit(PHOOK_LOG_BUFFER *OutBuffer);
VOID     HookLogDestroy(PHOOK_LOG_BUFFER Buffer);
VOID     HookLogWrite(PHOOK_LOG_BUFFER Buffer, PHOOK_LOG_ENTRY Entry);
ULONG    HookLogRead(PHOOK_LOG_BUFFER Buffer, PHOOK_LOG_ENTRY OutEntries, ULONG MaxEntries);
VOID     HookLogClear(PHOOK_LOG_BUFFER Buffer);
VOID     HookLogGetStats(PHOOK_LOG_BUFFER Buffer, PHOOK_LOG_STATS Stats);
