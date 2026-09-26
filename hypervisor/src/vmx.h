#pragma once
#include <ntddk.h>
#include "ia32.h"
#include "ept.h"

// ── Per-processor VMX state ──
typedef struct _VMX_CPU_STATE {
    BOOLEAN     Virtualized;
    PVOID       VmxonRegion;        // 4KB aligned VMXON region
    ULONG64     VmxonRegionPA;
    PVOID       VmcsRegion;         // 4KB aligned VMCS
    ULONG64     VmcsRegionPA;
    PVOID       MsrBitmap;          // 4KB MSR bitmap (all zeroes = no MSR exits)
    ULONG64     MsrBitmapPA;
    PVOID       HostStack;          // stack for VM exit handler
    ULONG64     HostStackTop;       // RSP value for VM exit
    EPT_STATE   EptState;
} VMX_CPU_STATE, *PVMX_CPU_STATE;

// ── Global state ──
typedef struct _HV_GLOBAL {
    ULONG           CpuCount;
    PVMX_CPU_STATE  CpuStates;      // array[CpuCount]
    BOOLEAN         Running;
} HV_GLOBAL, *PHV_GLOBAL;

extern HV_GLOBAL g_Hv;

// ── VMX operations ──
NTSTATUS VmxInitialize(void);
void     VmxTerminate(void);
NTSTATUS VmxVirtualizeCpu(ULONG CpuIndex);
void     VmxDevirtualizeCpu(ULONG CpuIndex);

// ── VM exit handler (called from assembly stub) ──
BOOLEAN  VmxExitHandler(PVMX_CPU_STATE CpuState);

// ── Assembly routines (defined in asm.asm) ──
extern void AsmVmxLaunch(void);
extern void AsmVmxEntryPoint(void);
extern ULONG64 AsmVmxVmCall(ULONG64 HypercallId, ULONG64 Param1, ULONG64 Param2);

// ── Segment helpers ──
ULONG   GetSegmentAccessRights(USHORT Selector);
ULONG64 GetSegmentBase(ULONG64 GdtBase, USHORT Selector);

// ── VMCS field adjustment ──
ULONG AdjustControls(ULONG RequestedValue, ULONG Msr);
