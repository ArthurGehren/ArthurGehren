#pragma once
#include <ntddk.h>

#define LOG_TAG "[HvLog] "

// Log entry types
typedef enum _LOG_TYPE {
    LogRtlPcToFileHeader = 1,
    LogMmGetSystemRoutineAddress = 2,
} LOG_TYPE;

// Ring buffer entry
#pragma pack(push, 1)
typedef struct _LOG_ENTRY {
    ULONG64     Timestamp;
    LOG_TYPE    Type;
    ULONG       ProcessId;
    ULONG       ThreadId;
    ULONG64     ReturnAddress;
    union {
        struct {
            ULONG64 PcAddress;              // address being resolved
            ULONG64 ResolvedBase;           // result (module base)
        } PcToFileHeader;
        struct {
            WCHAR   FunctionName[128];      // function name queried
            ULONG64 ResolvedAddress;        // result
        } GetRoutineAddress;
    };
} LOG_ENTRY, *PLOG_ENTRY;
#pragma pack(pop)

#define LOG_BUFFER_CAPACITY  4096

typedef struct _LOG_BUFFER {
    volatile LONG   Head;
    volatile LONG   Tail;
    volatile LONG   Count;
    LOG_ENTRY       Entries[LOG_BUFFER_CAPACITY];
} LOG_BUFFER, *PLOG_BUFFER;

NTSTATUS LogInitialize(void);
void     LogDestroy(void);
void     LogWrite(PLOG_ENTRY Entry);

// Convenience macros
#define HvLog(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, LOG_TAG fmt "\n", ##__VA_ARGS__)

#define HvLogError(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, LOG_TAG "ERROR: " fmt "\n", ##__VA_ARGS__)
