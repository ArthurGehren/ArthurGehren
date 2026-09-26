// ============================================================
// setup.exe — Instalador do SimpleHypervisor
// Compila com MSYS2 UCRT64:
//   gcc -o setup.exe src/setup.c -ladvapi32 -luser32
//
// Faz:
//   1. Copia o .sys para System32\drivers
//   2. Cria o serviço kernel
//   3. Habilita Test Signing (bcdedit)
//   4. Reinicia o PC
// ============================================================

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

#define DRIVER_NAME       "SimpleHypervisor"
#define DRIVER_FILENAME   "SimpleHypervisor.sys"
#define SERVICE_NAME      "SimpleHV"

static BOOL IsElevated(void)
{
    BOOL elevated = FALSE;
    HANDLE token = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev;
        DWORD size;
        if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size))
            elevated = elev.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated;
}

static BOOL FileExists(const char *path)
{
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static int RunCommand(const char *cmd)
{
    printf("  > %s\n", cmd);
    return system(cmd);
}

static BOOL InstallDriver(const char *sysPath)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        fprintf(stderr, "[!] OpenSCManager failed: %lu\n", GetLastError());
        return FALSE;
    }

    // Remove existing service if present
    SC_HANDLE existing = OpenServiceA(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (existing) {
        SERVICE_STATUS ss;
        ControlService(existing, SERVICE_CONTROL_STOP, &ss);
        Sleep(500);
        DeleteService(existing);
        CloseServiceHandle(existing);
        printf("[*] Removed previous service.\n");
        Sleep(200);
    }

    SC_HANDLE svc = CreateServiceA(
        scm,
        SERVICE_NAME,
        DRIVER_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_BOOT_START,      // Start at boot
        SERVICE_ERROR_NORMAL,
        sysPath,
        NULL, NULL, NULL, NULL, NULL);

    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            printf("[*] Service already exists, updating...\n");
            svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
            if (svc) {
                ChangeServiceConfigA(svc,
                    SERVICE_KERNEL_DRIVER,
                    SERVICE_BOOT_START,
                    SERVICE_ERROR_NORMAL,
                    sysPath,
                    NULL, NULL, NULL, NULL, NULL, DRIVER_NAME);
            }
        } else {
            fprintf(stderr, "[!] CreateService failed: %lu\n", err);
            CloseServiceHandle(scm);
            return FALSE;
        }
    }

    printf("[+] Service '%s' installed (BOOT_START).\n", SERVICE_NAME);
    printf("    Binary: %s\n", sysPath);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return TRUE;
}

static BOOL EnableTestSigning(void)
{
    printf("\n[*] Enabling Test Signing mode...\n");

    int ret = RunCommand("bcdedit /set testsigning on");
    if (ret != 0) {
        fprintf(stderr, "[!] bcdedit failed. Secure Boot may need to be disabled in BIOS.\n");
        return FALSE;
    }

    printf("[+] Test Signing enabled.\n");
    return TRUE;
}

static BOOL DisableDriverSignatureEnforcement(void)
{
    printf("[*] Disabling driver signature enforcement for next boot...\n");
    RunCommand("bcdedit /set nointegritychecks on");
    return TRUE;
}

static void PrintBanner(void)
{
    printf("\n");
    printf("  ============================================\n");
    printf("    SimpleHypervisor Setup\n");
    printf("    Monitor: RtlPcToFileHeader\n");
    printf("             MmGetSystemRoutineAddress\n");
    printf("  ============================================\n");
    printf("\n");
}

static void PrintUsage(const char *exe)
{
    printf("Usage:\n");
    printf("  %s install           Install driver and reboot\n", exe);
    printf("  %s install --no-reboot  Install without rebooting\n", exe);
    printf("  %s uninstall         Remove driver and clean up\n", exe);
    printf("  %s start             Start the driver (if not boot-start)\n", exe);
    printf("  %s stop              Stop the driver\n", exe);
    printf("  %s status            Show driver status\n", exe);
    printf("\n");
}

static BOOL CopyDriverToSystem(const char *srcDriver, char *destPath, DWORD destSize)
{
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    snprintf(destPath, destSize, "%s\\drivers\\%s", sysDir, DRIVER_FILENAME);

    printf("[*] Copying driver...\n");
    printf("    From: %s\n", srcDriver);
    printf("    To:   %s\n", destPath);

    if (!CopyFileA(srcDriver, destPath, FALSE)) {
        fprintf(stderr, "[!] CopyFile failed: %lu\n", GetLastError());
        return FALSE;
    }

    printf("[+] Driver copied.\n");
    return TRUE;
}

static void DoReboot(void)
{
    printf("\n[*] System will reboot in 5 seconds...\n");
    printf("    The hypervisor will be active after reboot.\n");
    printf("    Use hvlog.exe to read the captured events.\n\n");

    HANDLE token;
    TOKEN_PRIVILEGES tp;
    if (OpenProcessToken(GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        LookupPrivilegeValueA(NULL, "SeShutdownPrivilege", &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(token);
    }

    // 5-second delay for user to read the message
    Sleep(5000);

    if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_FLAG_PLANNED)) {
        // Fallback
        RunCommand("shutdown /r /t 0 /f");
    }
}

static int DoInstall(int argc, char *argv[])
{
    BOOL noReboot = FALSE;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--no-reboot") == 0)
            noReboot = TRUE;
    }

    // Find the .sys file — look next to this exe, then current dir
    char exeDir[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    char *lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char sysPath[MAX_PATH];
    snprintf(sysPath, MAX_PATH, "%s%s", exeDir, DRIVER_FILENAME);

    if (!FileExists(sysPath)) {
        // Try current directory
        snprintf(sysPath, MAX_PATH, ".\\%s", DRIVER_FILENAME);
        if (!FileExists(sysPath)) {
            fprintf(stderr, "[!] Cannot find %s\n", DRIVER_FILENAME);
            fprintf(stderr, "    Place it next to setup.exe or in the current directory.\n");
            fprintf(stderr, "\n    Build the driver first with WDK:\n");
            fprintf(stderr, "      build_driver.bat\n");
            return 1;
        }
    }

    // Get absolute path
    char fullSysPath[MAX_PATH];
    GetFullPathNameA(sysPath, MAX_PATH, fullSysPath, NULL);

    // Copy to System32\drivers
    char destPath[MAX_PATH];
    if (!CopyDriverToSystem(fullSysPath, destPath, MAX_PATH))
        return 1;

    // Install service
    if (!InstallDriver(destPath))
        return 1;

    // Enable test signing
    EnableTestSigning();
    DisableDriverSignatureEnforcement();

    if (noReboot) {
        printf("\n[+] Installation complete. Reboot manually to activate.\n");
        printf("    After reboot, run: hvlog.exe --follow\n");
    } else {
        DoReboot();
    }

    return 0;
}

static int DoUninstall(void)
{
    printf("[*] Uninstalling %s...\n", DRIVER_NAME);

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        fprintf(stderr, "[!] OpenSCManager failed: %lu\n", GetLastError());
        return 1;
    }

    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (svc) {
        SERVICE_STATUS ss;
        if (ControlService(svc, SERVICE_CONTROL_STOP, &ss))
            printf("[*] Service stopped.\n");
        Sleep(500);
        if (DeleteService(svc))
            printf("[+] Service deleted.\n");
        else
            fprintf(stderr, "[!] DeleteService failed: %lu\n", GetLastError());
        CloseServiceHandle(svc);
    } else {
        printf("[*] Service not found.\n");
    }

    CloseServiceHandle(scm);

    // Remove the .sys from System32\drivers
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    char driverPath[MAX_PATH];
    snprintf(driverPath, MAX_PATH, "%s\\drivers\\%s", sysDir, DRIVER_FILENAME);

    if (DeleteFileA(driverPath))
        printf("[+] Removed %s\n", driverPath);

    printf("[+] Uninstall complete.\n");
    return 0;
}

static int DoStart(void)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_START);
    if (!svc) {
        fprintf(stderr, "[!] Cannot open service: %lu\n", GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }

    if (StartServiceA(svc, 0, NULL))
        printf("[+] Driver started.\n");
    else
        fprintf(stderr, "[!] StartService failed: %lu\n", GetLastError());

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int DoStop(void)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!svc) {
        fprintf(stderr, "[!] Cannot open service: %lu\n", GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }

    SERVICE_STATUS ss;
    if (ControlService(svc, SERVICE_CONTROL_STOP, &ss))
        printf("[+] Driver stopped.\n");
    else
        fprintf(stderr, "[!] ControlService failed: %lu\n", GetLastError());

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int DoStatus(void)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!svc) {
        printf("[*] Service '%s' not installed.\n", SERVICE_NAME);
        CloseServiceHandle(scm);
        return 0;
    }

    SERVICE_STATUS ss;
    if (QueryServiceStatus(svc, &ss)) {
        const char *state = "Unknown";
        switch (ss.dwCurrentState) {
        case SERVICE_STOPPED:         state = "STOPPED"; break;
        case SERVICE_RUNNING:         state = "RUNNING"; break;
        case SERVICE_START_PENDING:   state = "START_PENDING"; break;
        case SERVICE_STOP_PENDING:    state = "STOP_PENDING"; break;
        }
        printf("[*] Service '%s': %s\n", SERVICE_NAME, state);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    // Also check if device is accessible
    HANDLE dev = CreateFileW(L"\\\\.\\SimpleHypervisor",
        GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (dev != INVALID_HANDLE_VALUE) {
        printf("[+] Device is accessible — hypervisor is active.\n");
        CloseHandle(dev);
    } else {
        printf("[-] Device not accessible — hypervisor not loaded.\n");
    }

    return 0;
}

int main(int argc, char *argv[])
{
    PrintBanner();

    if (!IsElevated()) {
        fprintf(stderr, "[!] This tool requires Administrator privileges.\n");
        fprintf(stderr, "    Right-click -> Run as administrator\n");
        return 1;
    }

    if (argc < 2) {
        PrintUsage(argv[0]);
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0)
        return DoInstall(argc, argv);
    else if (strcmp(cmd, "uninstall") == 0)
        return DoUninstall();
    else if (strcmp(cmd, "start") == 0)
        return DoStart();
    else if (strcmp(cmd, "stop") == 0)
        return DoStop();
    else if (strcmp(cmd, "status") == 0)
        return DoStatus();
    else {
        fprintf(stderr, "[!] Unknown command: %s\n", cmd);
        PrintUsage(argv[0]);
        return 1;
    }
}
