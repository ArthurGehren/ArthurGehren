// ============================================================
// client.c — User-mode log reader
// Abre o device do hypervisor e imprime os eventos capturados
// Compile: cl client.c /Fe:hvlog.exe
// ============================================================

#include <windows.h>
#include <stdio.h>

#define IOCTL_READ_LOG  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CLEAR_LOG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

#define LOG_RTLPC_TO_FILE_HEADER    1
#define LOG_MM_GET_SYSTEM_ROUTINE   2

#pragma pack(push, 1)
typedef struct _LOG_ENTRY {
    LARGE_INTEGER   Timestamp;
    DWORD           Type;
    DWORD           ProcessId;
    DWORD           ThreadId;
    ULONGLONG       CallerRip;
    ULONGLONG       ReturnAddress;
    union {
        struct {
            ULONGLONG PcAddress;
        } RtlPcToFileHeader;
        struct {
            WCHAR RoutineName[64];
        } MmGetSystemRoutine;
    } Detail;
} LOG_ENTRY;
#pragma pack(pop)

static const char* EventTypeName(DWORD type)
{
    switch (type) {
    case LOG_RTLPC_TO_FILE_HEADER:  return "RtlPcToFileHeader";
    case LOG_MM_GET_SYSTEM_ROUTINE: return "MmGetSystemRoutineAddress";
    default: return "Unknown";
    }
}

int main(int argc, char* argv[])
{
    BOOL continuous = FALSE;
    if (argc > 1 && strcmp(argv[1], "--follow") == 0)
        continuous = TRUE;

    HANDLE hDevice = CreateFileW(
        L"\\\\.\\SimpleHypervisor",
        GENERIC_READ, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[!] Cannot open device: error %lu\n", GetLastError());
        fprintf(stderr, "    Make sure the driver is loaded.\n");
        return 1;
    }

    printf("========================================\n");
    printf("  SimpleHypervisor Log Reader\n");
    printf("========================================\n\n");

    if (continuous)
        printf("[*] Following log (Ctrl+C to stop)...\n\n");

    LOG_ENTRY entries[256];
    DWORD totalEvents = 0;

    do {
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            hDevice, IOCTL_READ_LOG,
            NULL, 0,
            entries, sizeof(entries),
            &bytesReturned, NULL);

        if (!ok) {
            fprintf(stderr, "[!] IOCTL failed: %lu\n", GetLastError());
            break;
        }

        DWORD count = bytesReturned / sizeof(LOG_ENTRY);
        for (DWORD i = 0; i < count; i++) {
            LOG_ENTRY* e = &entries[i];
            totalEvents++;

            printf("[%5u] %-30s  PID=%-5u TID=%-5u\n",
                totalEvents,
                EventTypeName(e->Type),
                e->ProcessId,
                e->ThreadId);

            printf("        Caller RIP:     0x%016llX\n", e->CallerRip);
            printf("        Return Address: 0x%016llX\n", e->ReturnAddress);

            if (e->Type == LOG_RTLPC_TO_FILE_HEADER) {
                printf("        PC Address:    0x%016llX\n",
                    e->Detail.RtlPcToFileHeader.PcAddress);
            }
            else if (e->Type == LOG_MM_GET_SYSTEM_ROUTINE) {
                printf("        Routine Name:  %ws\n",
                    e->Detail.MmGetSystemRoutine.RoutineName);
            }

            // Timestamp (100ns units since 1601)
            SYSTEMTIME st;
            FileTimeToSystemTime((FILETIME*)&e->Timestamp, &st);
            printf("        Time:          %04d-%02d-%02d %02d:%02d:%02d.%03d\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            printf("\n");
        }

        if (continuous && count == 0)
            Sleep(500);

    } while (continuous);

    if (!continuous) {
        printf("[*] Total events read: %u\n", totalEvents);
    }

    CloseHandle(hDevice);
    return 0;
}
