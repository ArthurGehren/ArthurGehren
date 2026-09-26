#include "../include/hv.h"

// ============================================================
// VMX init, VMCS setup, VM-exit handler
// ============================================================

extern NTSTATUS EptInit(PEPT_STATE Ept);
extern NTSTATUS EptInstallHook(PEPT_STATE Ept, UINT64 TargetVA,
    BOOLEAN IsRtlPcToFileHeader, BOOLEAN IsMmGetSystemRoutineAddress);
extern BOOLEAN  EptHandleViolation(PEPT_STATE Ept, UINT64 GuestPhysAddr,
    UINT64 GuestRip, PGUEST_REGS GuestRegs, PLOG_BUFFER Log);

static UINT32 AdjustControls(UINT32 value, UINT32 msr)
{
    LARGE_INTEGER msrValue;
    msrValue.QuadPart = __readmsr(msr);
    value |= msrValue.LowPart;   // mandatory 1 bits
    value &= msrValue.HighPart;  // mandatory 0 bits
    return value;
}

static UINT16 GetSegmentAccessRights(UINT16 selector)
{
    if (selector == 0)
        return 0x10000; // unusable

    UINT64 ar;
    ar = __lar(selector);
    ar >>= 8;
    ar &= 0xF0FF;
    return (UINT16)ar;
}

BOOLEAN VmxCheckSupport(void)
{
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    if (!(cpuInfo[2] & (1 << 5))) // ECX.VMX
        return FALSE;

    UINT64 featureCtrl = __readmsr(IA32_FEATURE_CONTROL);
    if (!(featureCtrl & 1)) // lock bit
        return FALSE;
    if (!(featureCtrl & (1 << 2))) // VMX outside SMX
        return FALSE;

    return TRUE;
}

NTSTATUS VmxAllocateRegions(PVCPU Vcpu)
{
    PHYSICAL_ADDRESS maxAddr;
    maxAddr.QuadPart = MAXULONG64;

    UINT32 revisionId = (UINT32)__readmsr(IA32_VMX_BASIC);

    // VMXON region
    Vcpu->VmxonRegion = (PVMXON_REGION)MmAllocateContiguousMemory(
        PAGE_SIZE, maxAddr);
    if (!Vcpu->VmxonRegion)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Vcpu->VmxonRegion, PAGE_SIZE);
    Vcpu->VmxonRegion->RevisionId = revisionId;
    Vcpu->VmxonRegionPA =
        MmGetPhysicalAddress(Vcpu->VmxonRegion).QuadPart;

    // VMCS region
    Vcpu->VmcsRegion = (PVMCS_REGION)MmAllocateContiguousMemory(
        PAGE_SIZE, maxAddr);
    if (!Vcpu->VmcsRegion)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Vcpu->VmcsRegion, PAGE_SIZE);
    Vcpu->VmcsRegion->RevisionId = revisionId;
    Vcpu->VmcsRegionPA =
        MmGetPhysicalAddress(Vcpu->VmcsRegion).QuadPart;

    // Host stack (16KB)
    Vcpu->HostStack = ExAllocatePool2(
        POOL_FLAG_NON_PAGED, 0x4000, 'kStH');
    if (!Vcpu->HostStack)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Vcpu->HostStack, 0x4000);
    Vcpu->HostStackTop = (UINT64)Vcpu->HostStack + 0x4000 - 8;

    return STATUS_SUCCESS;
}

VOID VmxFreeRegions(PVCPU Vcpu)
{
    if (Vcpu->VmxonRegion)
        MmFreeContiguousMemory(Vcpu->VmxonRegion);
    if (Vcpu->VmcsRegion)
        MmFreeContiguousMemory(Vcpu->VmcsRegion);
    if (Vcpu->HostStack)
        ExFreePoolWithTag(Vcpu->HostStack, 'kStH');
}

// The VM-exit handler — called from the asm stub
BOOLEAN VmExitHandler(PGUEST_REGS GuestRegs)
{
    ULONG exitReason;
    __vmx_vmread(VMCS_EXIT_REASON, (size_t*)&exitReason);
    exitReason &= 0xFFFF;

    UINT64 guestRip, instrLen;
    __vmx_vmread(VMCS_GUEST_RIP, (size_t*)&guestRip);
    __vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN, (size_t*)&instrLen);

    switch (exitReason) {

    case EXIT_REASON_EPT_VIOLATION: {
        UINT64 guestPA, qualification;
        __vmx_vmread(VMCS_EXIT_GUEST_PHYSICAL_ADDR, (size_t*)&guestPA);
        __vmx_vmread(VMCS_EXIT_QUALIFICATION, (size_t*)&qualification);

        if (EptHandleViolation(&g_Hv.Ept, guestPA, guestRip, GuestRegs, g_Hv.Log)) {
            // For exec violations on hooked pages, enable Monitor Trap Flag
            // to single-step one instruction then re-engage the hook
            if (qualification & (1 << 2)) { // execute access
                UINT64 procCtls;
                __vmx_vmread(VMCS_CTRL_PROC_BASED, (size_t*)&procCtls);
                procCtls |= (1UL << 27); // monitor trap flag
                __vmx_vmwrite(VMCS_CTRL_PROC_BASED, procCtls);
            }
            return TRUE; // resume guest
        }
        // Unhandled EPT violation — inject #PF
        break;
    }

    case EXIT_REASON_CPUID: {
        int cpuInfo[4];
        __cpuidex(cpuInfo, (int)GuestRegs->Rax, (int)GuestRegs->Rcx);
        GuestRegs->Rax = cpuInfo[0];
        GuestRegs->Rbx = cpuInfo[1];
        GuestRegs->Rcx = cpuInfo[2];
        GuestRegs->Rdx = cpuInfo[3];
        __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
        return TRUE;
    }

    case EXIT_REASON_VMCALL: {
        // VMCALL with magic = devirtualize request
        if (GuestRegs->Rcx == 0xDEADC0DE) {
            // Return to guest after VMXOFF
            __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
            return FALSE; // signal to asm stub: do VMXOFF
        }
        __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
        return TRUE;
    }

    case EXIT_REASON_MSR_READ: {
        UINT32 msr = (UINT32)GuestRegs->Rcx;
        ULARGE_INTEGER val;
        val.QuadPart = __readmsr(msr);
        GuestRegs->Rax = val.LowPart;
        GuestRegs->Rdx = val.HighPart;
        __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
        return TRUE;
    }

    case EXIT_REASON_MSR_WRITE: {
        UINT32 msr = (UINT32)GuestRegs->Rcx;
        UINT64 val = (GuestRegs->Rdx << 32) | (GuestRegs->Rax & 0xFFFFFFFF);
        __writemsr(msr, val);
        __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
        return TRUE;
    }

    case EXIT_REASON_XSETBV: {
        _xsetbv((UINT32)GuestRegs->Rcx,
                (GuestRegs->Rdx << 32) | (GuestRegs->Rax & 0xFFFFFFFF));
        __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
        return TRUE;
    }

    // Monitor Trap Flag — one instruction was single-stepped
    case 37: { // EXIT_REASON_MTF
        // Re-engage the EPT hook
        for (UINT32 i = 0; i < g_Hv.Ept.HookCount; i++) {
            PEPT_HOOK_ENTRY hook = &g_Hv.Ept.Hooks[i];
            if (hook->State == HOOK_SINGLESTEP) {
                hook->PteEntry->Fields.Execute = 0;
                hook->State = HOOK_ACTIVE;
            }
        }
        struct {
            UINT64 Eptp;
            UINT64 Reserved;
        } desc = { g_Hv.Ept.Eptp.Value, 0 };
        __invept(1, &desc);

        // Clear MTF
        UINT64 procCtls;
        __vmx_vmread(VMCS_CTRL_PROC_BASED, (size_t*)&procCtls);
        procCtls &= ~(1UL << 27);
        __vmx_vmwrite(VMCS_CTRL_PROC_BASED, procCtls);
        return TRUE;
    }

    default:
        // Unhandled — advance RIP and hope for the best
        __vmx_vmwrite(VMCS_GUEST_RIP, guestRip + instrLen);
        return TRUE;
    }

    return TRUE;
}
