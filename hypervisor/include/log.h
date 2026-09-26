#pragma once
#include <ntddk.h>

// ============================================================
// Ring-buffer log — captura eventos no VMX root e expoe via IOCTL
// ============================================================

#define LOG_TAG                 'gLoH'
#define MAX_LOG_ENTRIES         4096
#define MAX_LOG_MESSAGE_LEN     256

#define IOCTL_READ_LOG  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CLEAR_LOG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef enum _LOG_EVENT_TYPE {
    LOG_RTLPC_TO_FILE_HEADER = 1,
    LOG_MM_GET_SYSTEM_ROUTINE = 2,
} LOG_EVENT_TYPE;

#pragma pack(push, 1)
typedef struct _LOG_ENTRY {
    LARGE_INTEGER   Timestamp;
    LOG_EVENT_TYPE  Type;
    UINT32          ProcessId;
    UINT32          ThreadId;
    UINT64          CallerRip;
    UINT64          ReturnAddress;
    union {
        struct {
            UINT64  PcAddress;
        } RtlPcToFileHeader;
        struct {
            WCHAR   RoutineName[64];
        } MmGetSystemRoutine;
    } Detail;
} LOG_ENTRY, *PLOG_ENTRY;
#pragma pack(pop)

typedef struct _LOG_BUFFER {
    LOG_ENTRY       Entries[MAX_LOG_ENTRIES];
    volatile LONG   Head;
    volatile LONG   Count;
    KSPIN_LOCK      Lock;
} LOG_BUFFER, *PLOG_BUFFER;

NTSTATUS LogInit(PLOG_BUFFER *OutBuffer);
VOID     LogDestroy(PLOG_BUFFER Buffer);
VOID     LogWrite(PLOG_BUFFER Buffer, PLOG_ENTRY Entry);
ULONG    LogRead(PLOG_BUFFER Buffer, PLOG_ENTRY OutEntries, ULONG MaxEntries);
VOID     LogClear(PLOG_BUFFER Buffer);
