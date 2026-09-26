#include "vmm.h"
#include "vmx.h"
#include "hooklog.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject);

PVMM_CONTEXT GlobalContext;
PHOOK_LOG_BUFFER g_HookLog = NULL;
PDEVICE_OBJECT g_DeviceObject = NULL;

static NTSTATUS IrpCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

static NTSTATUS IrpDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	ULONG bytesReturned = 0;

	if (!g_HookLog) {
		status = STATUS_DEVICE_NOT_READY;
		goto done;
	}

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_HV_READ_LOG: {
		ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
		ULONG maxEntries = outLen / sizeof(HOOK_LOG_ENTRY);
		if (maxEntries == 0) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		PHOOK_LOG_ENTRY outBuf = (PHOOK_LOG_ENTRY)Irp->AssociatedIrp.SystemBuffer;
		ULONG count = HookLogRead(g_HookLog, outBuf, maxEntries);
		bytesReturned = count * sizeof(HOOK_LOG_ENTRY);
		break;
	}
	case IOCTL_HV_CLEAR_LOG:
		HookLogClear(g_HookLog);
		break;
	case IOCTL_HV_GET_STATS: {
		if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(HOOK_LOG_STATS)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		PHOOK_LOG_STATS outStats = (PHOOK_LOG_STATS)Irp->AssociatedIrp.SystemBuffer;
		HookLogGetStats(g_HookLog, outStats);
		bytesReturned = sizeof(HOOK_LOG_STATS);
		break;
	}
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

done:
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = bytesReturned;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = DriverUnload;

	HvUtilLog("--------------------------------------------------------------\n");

	NTSTATUS logStatus = HookLogInit(&g_HookLog);
	if (!NT_SUCCESS(logStatus)) {
		HvUtilLogError("DriverEntry: Failed to initialize hook log buffer.\n");
		return logStatus;
	}

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\GbhvMonitor");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\DosDevices\\GbhvMonitor");

	NTSTATUS devStatus = IoCreateDevice(DriverObject, 0, &devName,
		FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_DeviceObject);
	if (NT_SUCCESS(devStatus)) {
		IoCreateSymbolicLink(&symLink, &devName);
		DriverObject->MajorFunction[IRP_MJ_CREATE] = IrpCreateClose;
		DriverObject->MajorFunction[IRP_MJ_CLOSE] = IrpCreateClose;
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpDeviceControl;
	} else {
		HvUtilLogError("DriverEntry: Failed to create device (0x%08X).\n", devStatus);
	}

	GlobalContext = HvInitializeAllProcessors();

	if(!GlobalContext)
	{
		return STATUS_SUCCESS;
	}

	return STATUS_SUCCESS;
}

VOID NTAPI ExitRootModeOnAllProcessors(_In_ struct _KDPC *Dpc,
	_In_opt_ PVOID DeferredContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2)
{
	SIZE_T CurrentProcessorNumber;
	PVMM_PROCESSOR_CONTEXT CurrentContext;

	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(DeferredContext);

	CurrentProcessorNumber = OsGetCurrentProcessorNumber();
	CurrentContext = HvGetCurrentCPUContext(GlobalContext);

	if (VmxExitRootMode(CurrentContext))
	{
		HvUtilLogDebug("ExitRootModeOnAllProcessors[#%i]: Exiting VMX mode.\n", CurrentProcessorNumber);
	}
	else
	{
		HvUtilLogError("ExitRootModeOnAllProcessors[#%i]: Failed to exit VMX mode.\n", CurrentProcessorNumber);
	}

	KeSignalCallDpcSynchronize(SystemArgument2);
	KeSignalCallDpcDone(SystemArgument1);
}

VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);

	if(GlobalContext)
	{
		KeGenericCallDpc(ExitRootModeOnAllProcessors, (PVOID)GlobalContext);
	}

	if (g_DeviceObject) {
		UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\DosDevices\\GbhvMonitor");
		IoDeleteSymbolicLink(&symLink);
		IoDeleteDevice(g_DeviceObject);
		g_DeviceObject = NULL;
	}

	if (g_HookLog) {
		HookLogDestroy(g_HookLog);
		g_HookLog = NULL;
	}
}
