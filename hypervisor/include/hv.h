#pragma once
#include "vmx.h"
#include "ept.h"
#include "log.h"

// ============================================================
// Per-CPU virtual machine state
// ============================================================

typedef struct _VCPU {
    BOOLEAN         Launched;

    PVMXON_REGION   VmxonRegion;
    UINT64          VmxonRegionPA;

    PVMCS_REGION    VmcsRegion;
    UINT64          VmcsRegionPA;

    PVOID           HostStack;
    UINT64          HostStackTop;

    GUEST_REGS      GuestRegs;
} VCPU, *PVCPU;

typedef struct _HV_CONTEXT {
    PVCPU           Vcpus;
    ULONG           CpuCount;
    EPT_STATE       Ept;
    PLOG_BUFFER     Log;
    BOOLEAN         Active;

    UINT64          RtlPcToFileHeaderVA;
    UINT64          MmGetSystemRoutineAddressVA;
} HV_CONTEXT, *PHV_CONTEXT;

extern HV_CONTEXT g_Hv;

NTSTATUS HvInit(void);
VOID     HvShutdown(void);
NTSTATUS HvVirtualizeCpu(ULONG CpuIndex);
VOID     HvDevirtualizeCpu(ULONG CpuIndex);
