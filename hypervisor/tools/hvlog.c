#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_HV_READ_LOG   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_HV_CLEAR_LOG  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_HV_GET_STATS  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

#define MAX_ROUTINE_NAME 64

typedef enum {
    HOOK_EVENT_RTLPC_TO_FILE_HEADER  = 1,
    HOOK_EVENT_MM_GET_SYSTEM_ROUTINE = 2,
} HOOK_EVENT_TYPE;

#pragma pack(push, 1)
typedef struct {
    LARGE_INTEGER   Timestamp;
    HOOK_EVENT_TYPE Type;
    uint32_t        ProcessId;
    uint32_t        ThreadId;
    uint64_t        CallerRip;
    uint64_t        ReturnAddress;
    union {
        struct {
            uint64_t PcAddress;
            uint64_t ResolvedBase;
        } RtlPcToFileHeader;
        struct {
            WCHAR RoutineName[MAX_ROUTINE_NAME];
        } MmGetSystemRoutine;
    } Detail;
} HOOK_LOG_ENTRY;

typedef struct {
    uint64_t TotalRtlPcToFileHeader;
    uint64_t TotalMmGetSystemRoutine;
    uint32_t CurrentEntries;
    uint32_t DroppedEntries;
} HOOK_LOG_STATS;
#pragma pack(pop)

static void SetConsoleColors(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
}

static void PrintHeader(void)
{
    printf("=============================================================\n");
    printf("  GBHV Hook Monitor - Real-Time Log Viewer\n");
    printf("  RtlPcToFileHeader + MmGetSystemRoutineAddress\n");
    printf("=============================================================\n\n");
    printf("  Press Ctrl+C to exit\n\n");
}

static void PrintEntry(const HOOK_LOG_ENTRY *e)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    SYSTEMTIME st;
    FILETIME ft;
    ft.dwLowDateTime = e->Timestamp.LowPart;
    ft.dwHighDateTime = (DWORD)e->Timestamp.HighPart;
    FileTimeToSystemTime(&ft, &st);

    if (e->Type == HOOK_EVENT_RTLPC_TO_FILE_HEADER) {
        SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        printf("[%02d:%02d:%02d.%03d] RtlPcToFileHeader  PID=%u TID=%u\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            e->ProcessId, e->ThreadId);
        SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        printf("    CallerRIP=0x%016llX  RetAddr=0x%016llX\n",
            (unsigned long long)e->CallerRip,
            (unsigned long long)e->ReturnAddress);
        printf("    PcAddress=0x%016llX  ResolvedBase=0x%016llX\n",
            (unsigned long long)e->Detail.RtlPcToFileHeader.PcAddress,
            (unsigned long long)e->Detail.RtlPcToFileHeader.ResolvedBase);
    } else if (e->Type == HOOK_EVENT_MM_GET_SYSTEM_ROUTINE) {
        SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        printf("[%02d:%02d:%02d.%03d] MmGetSystemRoutine  PID=%u TID=%u\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            e->ProcessId, e->ThreadId);
        SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        printf("    CallerRIP=0x%016llX  RetAddr=0x%016llX\n",
            (unsigned long long)e->CallerRip,
            (unsigned long long)e->ReturnAddress);
        printf("    RoutineName=%ls\n", e->Detail.MmGetSystemRoutine.RoutineName);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    SetConsoleTitleA("GBHV Hook Monitor");
    SetConsoleColors();
    PrintHeader();

    HANDLE hDev = CreateFileA("\\\\.\\GbhvMonitor",
        GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_INTENSITY);
        printf("[ERROR] Cannot open \\\\.\\GbhvMonitor (error %lu)\n", GetLastError());
        printf("        Make sure the driver is loaded and running.\n");
        printf("        Run setup.exe as Administrator first.\n\n");
        SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        printf("Press any key to exit...\n");
        getchar();
        return 1;
    }

    printf("[OK] Connected to GbhvMonitor device\n\n");

    HOOK_LOG_ENTRY entries[64];
    DWORD bytesReturned;
    uint64_t totalShown = 0;

    while (1) {
        BOOL ok = DeviceIoControl(hDev, IOCTL_HV_READ_LOG,
            NULL, 0,
            entries, sizeof(entries),
            &bytesReturned, NULL);

        if (ok && bytesReturned > 0) {
            ULONG count = bytesReturned / sizeof(HOOK_LOG_ENTRY);
            for (ULONG i = 0; i < count; i++) {
                PrintEntry(&entries[i]);
                totalShown++;
            }
        }

        if (totalShown > 0 && (totalShown % 100) == 0) {
            HOOK_LOG_STATS stats;
            DWORD sBytes;
            if (DeviceIoControl(hDev, IOCTL_HV_GET_STATS,
                NULL, 0, &stats, sizeof(stats), &sBytes, NULL) && sBytes == sizeof(stats)) {
                HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                printf("--- Stats: RtlPc=%llu MmGet=%llu Queued=%u Dropped=%u ---\n\n",
                    (unsigned long long)stats.TotalRtlPcToFileHeader,
                    (unsigned long long)stats.TotalMmGetSystemRoutine,
                    stats.CurrentEntries, stats.DroppedEntries);
                SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            }
        }

        Sleep(250);
    }

    CloseHandle(hDev);
    return 0;
}
