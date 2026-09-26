/*
 * driver.c - Minimal Intel VT-x Hypervisor for Function Call Logging
 *
 * Intercepts:
 *   1. RtlPcToFileHeader   - logs caller, queried PC address, resolved module base
 *   2. MmGetSystemRoutineAddress - logs function name being resolved
 *
 * Uses EPT (Extended Page Tables) shadow-page hooks for invisible interception.
 * No hardware virtualization beyond EPT - purely a monitoring/logging tool.
 */

#include <ntddk.h>
#include "vmx.h"
#include "log.h"

#define DEVICE_NAME     L"\\Device\\HvMonitor"
#define SYMLINK_NAME    L"\\DosDevices\\HvMonitor"

static PDEVICE_OBJECT g_DeviceObject = NULL;
static UNICODE_STRING g_DeviceName;
static UNICODE_STRING g_SymLink;

// Forward declarations
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverUnload;

static NTSTATUS DeviceCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS DeviceIoControl(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    // Placeholder for user-mode log retrieval IOCTL
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;

    HvLog("========================================");
    HvLog("HvMonitor - VT-x Function Call Logger");
    HvLog("Targets: RtlPcToFileHeader, MmGetSystemRoutineAddress");
    HvLog("========================================");

    // Initialize logging
    status = LogInitialize();
    if (!NT_SUCCESS(status)) {
        HvLogError("Failed to init log: 0x%X", status);
        return status;
    }

    // Create device object for user-mode communication
    RtlInitUnicodeString(&g_DeviceName, DEVICE_NAME);
    RtlInitUnicodeString(&g_SymLink, SYMLINK_NAME);

    status = IoCreateDevice(
        DriverObject, 0, &g_DeviceName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
        &g_DeviceObject);

    if (!NT_SUCCESS(status)) {
        HvLogError("IoCreateDevice failed: 0x%X", status);
        LogDestroy();
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLink, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        LogDestroy();
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DeviceCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = DeviceCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceIoControl;
    DriverObject->DriverUnload = DriverUnload;

    // Initialize and launch the hypervisor
    status = VmxInitialize();
    if (!NT_SUCCESS(status)) {
        HvLogError("VmxInitialize failed: 0x%X", status);
        IoDeleteSymbolicLink(&g_SymLink);
        IoDeleteDevice(g_DeviceObject);
        LogDestroy();
        return status;
    }

    HvLog("HvMonitor loaded and active — monitoring function calls");
    return STATUS_SUCCESS;
}

void DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    HvLog("HvMonitor unloading...");

    // Devirtualize all CPUs and remove hooks
    VmxTerminate();

    IoDeleteSymbolicLink(&g_SymLink);
    if (g_DeviceObject)
        IoDeleteDevice(g_DeviceObject);

    LogDestroy();

    HvLog("HvMonitor unloaded");
}
