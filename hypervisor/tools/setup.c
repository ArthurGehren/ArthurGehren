#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

#define DRIVER_NAME     "gbhv"
#define DRIVER_FILE     "gbhv.sys"
#define SERVICE_NAME    "gbhv"
#define DISPLAY_NAME    "GBHV Hypervisor Monitor"

static BOOL IsAdmin(void)
{
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID adminGroup = NULL;
    if (AllocateAndInitializeSid(&ntAuth, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0,0,0,0,0,0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

static BOOL CopyDriverToSystem(const char *srcDir)
{
    char src[MAX_PATH], dst[MAX_PATH];
    snprintf(src, MAX_PATH, "%s\\%s", srcDir, DRIVER_FILE);
    GetSystemDirectoryA(dst, MAX_PATH);
    strcat_s(dst, MAX_PATH, "\\drivers\\");
    strcat_s(dst, MAX_PATH, DRIVER_FILE);

    if (!CopyFileA(src, dst, FALSE)) {
        printf("[ERROR] Failed to copy %s -> %s (error %lu)\n", src, dst, GetLastError());
        return FALSE;
    }
    printf("[OK] Driver copied to %s\n", dst);
    return TRUE;
}

static BOOL InstallDriver(void)
{
    SC_HANDLE hScm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hScm) {
        printf("[ERROR] Cannot open SCM (error %lu)\n", GetLastError());
        return FALSE;
    }

    char driverPath[MAX_PATH];
    GetSystemDirectoryA(driverPath, MAX_PATH);
    strcat_s(driverPath, MAX_PATH, "\\drivers\\");
    strcat_s(driverPath, MAX_PATH, DRIVER_FILE);

    char sysPath[MAX_PATH + 16];
    snprintf(sysPath, sizeof(sysPath), "\\SystemRoot\\System32\\drivers\\%s", DRIVER_FILE);

    SC_HANDLE hSvc = OpenServiceA(hScm, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (hSvc) {
        printf("[INFO] Service already exists, updating...\n");
        SERVICE_STATUS svcStatus;
        ControlService(hSvc, SERVICE_CONTROL_STOP, &svcStatus);
        Sleep(500);
        DeleteService(hSvc);
        CloseServiceHandle(hSvc);
        Sleep(500);
    }

    hSvc = CreateServiceA(hScm, SERVICE_NAME, DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_SYSTEM_START,
        SERVICE_ERROR_NORMAL,
        sysPath,
        NULL, NULL, NULL, NULL, NULL);

    if (!hSvc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            printf("[INFO] Service already registered.\n");
        } else {
            printf("[ERROR] CreateService failed (error %lu)\n", err);
            CloseServiceHandle(hScm);
            return FALSE;
        }
    } else {
        printf("[OK] Driver service created (SYSTEM_START)\n");
        CloseServiceHandle(hSvc);
    }

    CloseServiceHandle(hScm);
    return TRUE;
}

static BOOL EnableTestSigning(void)
{
    printf("[INFO] Enabling test signing mode...\n");
    int ret = system("bcdedit /set testsigning on");
    if (ret != 0) {
        printf("[WARN] bcdedit returned %d. You may need to disable Secure Boot.\n", ret);
        return FALSE;
    }
    printf("[OK] Test signing enabled.\n");
    return TRUE;
}

static void CreateDesktopShortcut(const char *srcDir)
{
    char desktop[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
        printf("[WARN] Could not find desktop path.\n");
        return;
    }

    char hvlogSrc[MAX_PATH], hvlogDst[MAX_PATH];
    snprintf(hvlogSrc, MAX_PATH, "%s\\hvlog.exe", srcDir);
    snprintf(hvlogDst, MAX_PATH, "%s\\hvlog.exe", desktop);

    if (CopyFileA(hvlogSrc, hvlogDst, FALSE)) {
        printf("[OK] hvlog.exe copied to desktop: %s\n", hvlogDst);
    } else {
        printf("[WARN] Could not copy hvlog.exe to desktop (error %lu)\n", GetLastError());
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    SetConsoleTitleA("GBHV Setup");

    printf("=============================================================\n");
    printf("  GBHV Hypervisor Monitor - Setup\n");
    printf("=============================================================\n\n");

    if (!IsAdmin()) {
        printf("[ERROR] This program must be run as Administrator.\n");
        printf("        Right-click setup.exe and select 'Run as administrator'.\n\n");
        printf("Press any key to exit...\n");
        getchar();
        return 1;
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *lastSlash = '\0';

    printf("[1/4] Copying driver...\n");
    if (!CopyDriverToSystem(exePath)) {
        printf("\nSetup failed. Press any key to exit...\n");
        getchar();
        return 1;
    }

    printf("\n[2/4] Installing driver service...\n");
    if (!InstallDriver()) {
        printf("\nSetup failed. Press any key to exit...\n");
        getchar();
        return 1;
    }

    printf("\n[3/4] Enabling test signing...\n");
    EnableTestSigning();

    printf("\n[4/4] Creating desktop shortcut...\n");
    CreateDesktopShortcut(exePath);

    printf("\n=============================================================\n");
    printf("  Setup complete!\n");
    printf("  The system will reboot in 10 seconds.\n");
    printf("  After reboot, double-click hvlog.exe on your desktop\n");
    printf("  to see real-time hook logs.\n");
    printf("=============================================================\n\n");

    printf("Press any key to reboot now, or close the window to cancel...\n");
    getchar();

    system("shutdown /r /t 0");
    return 0;
}
