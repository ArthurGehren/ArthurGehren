#include "../include/hv.h"

// ============================================================
// Driver entry — inicializa o hypervisor, expoe device para logs
// ============================================================

HV_CONTEXT g_Hv = { 0 };

extern NTSTATUS EptInit(PEPT_STATE Ept);
extern NTSTATUS EptInstallHook(PEPT_STATE Ept, UINT64 TargetVA,
    BOOLEAN IsRtlPcToFileHeader, BOOLEAN IsMmGetSystemRoutineAddress);
extern BOOLEAN VmxCheckSupport(void);
extern NTSTATUS VmxAllocateRegions(PVCPU Vcpu);
extern VOID VmxFreeRegions(PVCPU Vcpu);

#define DEVICE_NAME     L"\\Device\\SimpleHypervisor"
#define SYMLINK_NAME    L"\\DosDevices\\SimpleHypervisor"

static PDEVICE_OBJECT g_DeviceObject = NULL;

// Resolve kernel exports for hooking
static NTSTATUS ResolveTargets(void)
{
    UNICODE_STRING name1 = RTL_CONSTANT_STRING(L"RtlPcToFileHeader");
    g_Hv.RtlPcToFileHeaderVA =
        (UINT64)MmGetSystemRoutineAddress(&name1);
    if (!g_Hv.RtlPcToFileHeaderVA) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] Failed to resolve RtlPcToFileHeader\n");
        return STATUS_NOT_FOUND;
    }

    UNICODE_STRING name2 = RTL_CONSTANT_STRING(L"MmGetSystemRoutineAddress");
    g_Hv.MmGetSystemRoutineAddressVA =
        (UINT64)MmGetSystemRoutineAddress(&name2);
    if (!g_Hv.MmGetSystemRoutineAddressVA) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] Failed to resolve MmGetSystemRoutineAddress\n");
        return STATUS_NOT_FOUND;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[HV] RtlPcToFileHeader          = 0x%llX\n",
        g_Hv.RtlPcToFileHeaderVA);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[HV] MmGetSystemRoutineAddress  = 0x%llX\n",
        g_Hv.MmGetSystemRoutineAddressVA);

    return STATUS_SUCCESS;
}

// IRP handlers
static NTSTATUS IrpCreateClose(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS IrpDeviceControl(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DevObj);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG info = 0;

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {

    case IOCTL_READ_LOG: {
        ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
        ULONG maxEntries = outLen / sizeof(LOG_ENTRY);
        if (maxEntries == 0) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        PLOG_ENTRY outBuf = (PLOG_ENTRY)Irp->AssociatedIrp.SystemBuffer;
        ULONG count = LogRead(g_Hv.Log, outBuf, maxEntries);
        info = count * sizeof(LOG_ENTRY);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_CLEAR_LOG:
        LogClear(g_Hv.Log);
        status = STATUS_SUCCESS;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// DPC callback to virtualize each CPU
static VOID VirtualizeCpuDpc(
    PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    ULONG cpuIndex = KeGetCurrentProcessorNumber();
    NTSTATUS status = HvVirtualizeCpu(cpuIndex);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] Failed to virtualize CPU %lu: 0x%X\n", cpuIndex, status);
    }
    *(PNTSTATUS)Context = status;
}

NTSTATUS HvInit(void)
{
    if (!VmxCheckSupport()) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] VMX not supported\n");
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS status = LogInit(&g_Hv.Log);
    if (!NT_SUCCESS(status)) return status;

    status = ResolveTargets();
    if (!NT_SUCCESS(status)) return status;

    status = EptInit(&g_Hv.Ept);
    if (!NT_SUCCESS(status)) return status;

    // Install EPT hooks
    status = EptInstallHook(&g_Hv.Ept,
        g_Hv.RtlPcToFileHeaderVA, TRUE, FALSE);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] Failed to hook RtlPcToFileHeader: 0x%X\n", status);
        return status;
    }

    status = EptInstallHook(&g_Hv.Ept,
        g_Hv.MmGetSystemRoutineAddressVA, FALSE, TRUE);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] Failed to hook MmGetSystemRoutineAddress: 0x%X\n", status);
        return status;
    }

    g_Hv.CpuCount = KeQueryActiveProcessorCount(NULL);
    g_Hv.Vcpus = (PVCPU)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(VCPU) * g_Hv.CpuCount, 'upCV');
    if (!g_Hv.Vcpus) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_Hv.Vcpus, sizeof(VCPU) * g_Hv.CpuCount);

    for (ULONG i = 0; i < g_Hv.CpuCount; i++) {
        status = VmxAllocateRegions(&g_Hv.Vcpus[i]);
        if (!NT_SUCCESS(status)) return status;
    }

    g_Hv.Active = TRUE;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[HV] Hypervisor initialized — %lu CPUs, 2 hooks active\n",
        g_Hv.CpuCount);

    return STATUS_SUCCESS;
}

VOID HvShutdown(void)
{
    g_Hv.Active = FALSE;

    // Devirtualize all CPUs via VMCALL
    for (ULONG i = 0; i < g_Hv.CpuCount; i++) {
        if (g_Hv.Vcpus[i].Launched) {
            HvDevirtualizeCpu(i);
        }
        VmxFreeRegions(&g_Hv.Vcpus[i]);
    }

    if (g_Hv.Vcpus)
        ExFreePoolWithTag(g_Hv.Vcpus, 'upCV');

    LogDestroy(g_Hv.Log);
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[HV] Unloading...\n");

    HvShutdown();

    UNICODE_STRING symlink = RTL_CONSTANT_STRING(SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlink);
    if (g_DeviceObject)
        IoDeleteDevice(g_DeviceObject);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[HV] Unloaded.\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "[HV] SimpleHypervisor loading...\n");

    // Create device
    UNICODE_STRING devName = RTL_CONSTANT_STRING(DEVICE_NAME);
    NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
        &g_DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    UNICODE_STRING symlink = RTL_CONSTANT_STRING(SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = IrpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = IrpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpDeviceControl;

    status = HvInit();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "[HV] Init failed: 0x%X\n", status);
        IoDeleteSymbolicLink(&symlink);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    return STATUS_SUCCESS;
}
