#include "vmx.h"
#include "hooks.h"
#include "log.h"

HV_GLOBAL g_Hv = { 0 };

#define HOST_STACK_SIZE  (8 * PAGE_SIZE)
#define VMCALL_UNHOOK    0x4856   // 'HV' - temporary unhook for original call

// ── Helpers ──

ULONG AdjustControls(ULONG RequestedValue, ULONG Msr)
{
    LARGE_INTEGER msrValue;
    msrValue.QuadPart = __readmsr(Msr);
    RequestedValue |= msrValue.LowPart;      // bits that must be 1
    RequestedValue &= msrValue.HighPart;      // bits that must be 0
    return RequestedValue;
}

static PVOID AllocAlignedPage(PULONG64 PhysOut)
{
    PVOID va = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, 'xmVH');
    if (va) {
        RtlZeroMemory(va, PAGE_SIZE);
        if (PhysOut)
            *PhysOut = MmGetPhysicalAddress(va).QuadPart;
    }
    return va;
}

// ── Segment descriptor helpers ──

ULONG64 GetSegmentBase(ULONG64 GdtBase, USHORT Selector)
{
    if (Selector == 0 || (Selector & 0x4)) // NULL or LDT
        return 0;

    PSEGMENT_DESCRIPTOR desc = (PSEGMENT_DESCRIPTOR)(GdtBase + (Selector & ~0x7));
    ULONG64 base = desc->BaseLow | ((ULONG64)desc->BaseMid << 16) | ((ULONG64)desc->BaseHigh << 24);

    // System segment (TSS, etc.) in 64-bit mode is 16 bytes
    UCHAR type = desc->Flags1 & 0x0F;
    BOOLEAN isSystem = !(desc->Flags1 & 0x10);
    if (isSystem && (type == 0x09 || type == 0x0B)) {
        PSEGMENT_DESCRIPTOR_64 desc64 = (PSEGMENT_DESCRIPTOR_64)desc;
        base |= ((ULONG64)desc64->BaseUpper << 32);
    }

    return base;
}

ULONG GetSegmentAccessRights(USHORT Selector)
{
    if (Selector == 0)
        return 0x10000; // unusable

    ULONG ar;
    // LAR instruction returns access rights shifted left by 8
    ar = __lar(Selector);
    ar >>= 8;
    ar &= 0xF0FF; // mask out reserved bits

    return ar;
}

// ── Check CPU support ──

static BOOLEAN VmxCheckSupport(void)
{
    CPUID_RESULT cpuid;
    __cpuid((int*)&cpuid, 1);

    if (!(cpuid.Ecx & (1 << 5))) {
        HvLogError("VMX not supported by CPU");
        return FALSE;
    }

    ULONG64 featureCtrl = __readmsr(MSR_IA32_FEATURE_CONTROL);
    if ((featureCtrl & FEATURE_CONTROL_LOCKED) &&
        !(featureCtrl & FEATURE_CONTROL_VMXON_OUTSIDE)) {
        HvLogError("VMX locked out in BIOS");
        return FALSE;
    }

    return TRUE;
}

// ── Initialize VMX globally ──

NTSTATUS VmxInitialize(void)
{
    if (!VmxCheckSupport())
        return STATUS_NOT_SUPPORTED;

    g_Hv.CpuCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    g_Hv.CpuStates = (PVMX_CPU_STATE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        g_Hv.CpuCount * sizeof(VMX_CPU_STATE),
        'tSvH');

    if (!g_Hv.CpuStates)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_Hv.CpuStates, g_Hv.CpuCount * sizeof(VMX_CPU_STATE));

    HvLog("VMX init: %u processors", g_Hv.CpuCount);

    // Virtualize each CPU
    for (ULONG i = 0; i < g_Hv.CpuCount; i++) {
        NTSTATUS status = VmxVirtualizeCpu(i);
        if (!NT_SUCCESS(status)) {
            HvLogError("Failed to virtualize CPU %u: 0x%X", i, status);
            VmxTerminate();
            return status;
        }
    }

    g_Hv.Running = TRUE;

    // Install our two hooks (only needs to be done once, EPT is shared concept
    // but each CPU has its own EPT state for per-CPU hooks tracking)
    NTSTATUS status = HooksInstall(&g_Hv.CpuStates[0].EptState);
    if (!NT_SUCCESS(status)) {
        HvLogError("Failed to install hooks: 0x%X", status);
        VmxTerminate();
        return status;
    }

    return STATUS_SUCCESS;
}

// ── Per-CPU virtualization ──

NTSTATUS VmxVirtualizeCpu(ULONG CpuIndex)
{
    PVMX_CPU_STATE cpu = &g_Hv.CpuStates[CpuIndex];
    NTSTATUS status;

    // Switch to target CPU
    GROUP_AFFINITY oldAffinity, newAffinity = { 0 };
    newAffinity.Group = 0;
    newAffinity.Mask = (KAFFINITY)(1ULL << CpuIndex);
    KeSetSystemGroupAffinityThread(&newAffinity, &oldAffinity);

    // Enable VMX in CR4
    ULONG64 cr4 = __readcr4();
    __writecr4(cr4 | CR4_VMXE);

    // Allocate VMXON region
    cpu->VmxonRegion = AllocAlignedPage(&cpu->VmxonRegionPA);
    if (!cpu->VmxonRegion) { status = STATUS_INSUFFICIENT_RESOURCES; goto fail; }

    // Write VMCS revision ID into VMXON region
    ULONG64 vmxBasic = __readmsr(MSR_IA32_VMX_BASIC);
    *(PULONG)cpu->VmxonRegion = (ULONG)(vmxBasic & 0x7FFFFFFF);

    // VMXON
    if (__vmx_on(&cpu->VmxonRegionPA) != 0) {
        HvLogError("VMXON failed on CPU %u", CpuIndex);
        status = STATUS_UNSUCCESSFUL;
        goto fail;
    }

    // Allocate and set up VMCS
    cpu->VmcsRegion = AllocAlignedPage(&cpu->VmcsRegionPA);
    if (!cpu->VmcsRegion) { status = STATUS_INSUFFICIENT_RESOURCES; goto fail_vmxoff; }
    *(PULONG)cpu->VmcsRegion = (ULONG)(vmxBasic & 0x7FFFFFFF);

    if (__vmx_vmclear(&cpu->VmcsRegionPA) != 0) {
        status = STATUS_UNSUCCESSFUL; goto fail_vmxoff;
    }
    if (__vmx_vmptrld(&cpu->VmcsRegionPA) != 0) {
        status = STATUS_UNSUCCESSFUL; goto fail_vmxoff;
    }

    // Allocate MSR bitmap (all zeroes = no MSR exits)
    cpu->MsrBitmap = AllocAlignedPage(&cpu->MsrBitmapPA);
    if (!cpu->MsrBitmap) { status = STATUS_INSUFFICIENT_RESOURCES; goto fail_vmxoff; }

    // Allocate host stack
    cpu->HostStack = ExAllocatePool2(POOL_FLAG_NON_PAGED, HOST_STACK_SIZE, 'kSvH');
    if (!cpu->HostStack) { status = STATUS_INSUFFICIENT_RESOURCES; goto fail_vmxoff; }
    cpu->HostStackTop = (ULONG64)cpu->HostStack + HOST_STACK_SIZE;

    // Initialize EPT for this CPU
    status = EptInitialize(&cpu->EptState);
    if (!NT_SUCCESS(status)) goto fail_vmxoff;

    // ── Set up VMCS fields ──

    // --- Control fields ---
    ULONG pinBased = AdjustControls(0, MSR_IA32_VMX_TRUE_PINBASED_CTLS);
    __vmx_vmwrite(VMCS_PIN_BASED_CONTROLS, pinBased);

    ULONG procBased = AdjustControls(
        PROC_BASED_USE_MSR_BITMAPS | PROC_BASED_ACTIVATE_SECONDARY,
        MSR_IA32_VMX_TRUE_PROCBASED_CTLS);
    __vmx_vmwrite(VMCS_PROC_BASED_CONTROLS, procBased);

    ULONG procBased2 = AdjustControls(
        PROC_BASED2_ENABLE_EPT | PROC_BASED2_ENABLE_VPID,
        MSR_IA32_VMX_PROCBASED_CTLS2);
    __vmx_vmwrite(VMCS_PROC_BASED_CONTROLS2, procBased2);

    ULONG exitCtls = AdjustControls(
        EXIT_CONTROL_HOST_ADDR_SPACE | EXIT_CONTROL_SAVE_EFER | EXIT_CONTROL_LOAD_EFER,
        MSR_IA32_VMX_TRUE_EXIT_CTLS);
    __vmx_vmwrite(VMCS_EXIT_CONTROLS, exitCtls);

    ULONG entryCtls = AdjustControls(
        ENTRY_CONTROL_IA32E_MODE | ENTRY_CONTROL_LOAD_EFER,
        MSR_IA32_VMX_TRUE_ENTRY_CTLS);
    __vmx_vmwrite(VMCS_ENTRY_CONTROLS, entryCtls);

    __vmx_vmwrite(VMCS_MSR_BITMAP, cpu->MsrBitmapPA);
    __vmx_vmwrite(VMCS_EPT_POINTER, EptGetPointer(&cpu->EptState).Value);
    __vmx_vmwrite(VMCS_VPID, 1);
    __vmx_vmwrite(VMCS_GUEST_VMCS_LINK_PTR, ~0ULL);

    // --- Guest state (capture current CPU state) ---
    DESCRIPTOR_TABLE_REG gdtr, idtr;
    _sgdt(&gdtr);
    __sidt(&idtr);

    __vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
    __vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());
    __vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());
    __vmx_vmwrite(VMCS_GUEST_DR7, __readdr(7));
    __vmx_vmwrite(VMCS_GUEST_RFLAGS, __readeflags());

    __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, gdtr.Base);
    __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, gdtr.Limit);
    __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, idtr.Base);
    __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, idtr.Limit);

    // CS
    USHORT cs = _readcs();
    __vmx_vmwrite(VMCS_GUEST_CS_SEL, cs);
    __vmx_vmwrite(VMCS_GUEST_CS_BASE, GetSegmentBase(gdtr.Base, cs));
    __vmx_vmwrite(VMCS_GUEST_CS_LIMIT, __segmentlimit(cs));
    __vmx_vmwrite(VMCS_GUEST_CS_ACCESS, GetSegmentAccessRights(cs));

    // SS
    USHORT ss = _readss();
    __vmx_vmwrite(VMCS_GUEST_SS_SEL, ss);
    __vmx_vmwrite(VMCS_GUEST_SS_BASE, GetSegmentBase(gdtr.Base, ss));
    __vmx_vmwrite(VMCS_GUEST_SS_LIMIT, __segmentlimit(ss));
    __vmx_vmwrite(VMCS_GUEST_SS_ACCESS, GetSegmentAccessRights(ss));

    // DS
    USHORT ds = _readds();
    __vmx_vmwrite(VMCS_GUEST_DS_SEL, ds);
    __vmx_vmwrite(VMCS_GUEST_DS_BASE, GetSegmentBase(gdtr.Base, ds));
    __vmx_vmwrite(VMCS_GUEST_DS_LIMIT, __segmentlimit(ds));
    __vmx_vmwrite(VMCS_GUEST_DS_ACCESS, GetSegmentAccessRights(ds));

    // ES
    USHORT es = _reades();
    __vmx_vmwrite(VMCS_GUEST_ES_SEL, es);
    __vmx_vmwrite(VMCS_GUEST_ES_BASE, GetSegmentBase(gdtr.Base, es));
    __vmx_vmwrite(VMCS_GUEST_ES_LIMIT, __segmentlimit(es));
    __vmx_vmwrite(VMCS_GUEST_ES_ACCESS, GetSegmentAccessRights(es));

    // FS
    USHORT fs = _readfs();
    __vmx_vmwrite(VMCS_GUEST_FS_SEL, fs);
    __vmx_vmwrite(VMCS_GUEST_FS_BASE, __readmsr(MSR_FS_BASE));
    __vmx_vmwrite(VMCS_GUEST_FS_LIMIT, __segmentlimit(fs));
    __vmx_vmwrite(VMCS_GUEST_FS_ACCESS, GetSegmentAccessRights(fs));

    // GS
    USHORT gs = _readgs();
    __vmx_vmwrite(VMCS_GUEST_GS_SEL, gs);
    __vmx_vmwrite(VMCS_GUEST_GS_BASE, __readmsr(MSR_GS_BASE));
    __vmx_vmwrite(VMCS_GUEST_GS_LIMIT, __segmentlimit(gs));
    __vmx_vmwrite(VMCS_GUEST_GS_ACCESS, GetSegmentAccessRights(gs));

    // TR
    USHORT tr = _readtr();
    __vmx_vmwrite(VMCS_GUEST_TR_SEL, tr);
    __vmx_vmwrite(VMCS_GUEST_TR_BASE, GetSegmentBase(gdtr.Base, tr));
    __vmx_vmwrite(VMCS_GUEST_TR_LIMIT, __segmentlimit(tr));
    __vmx_vmwrite(VMCS_GUEST_TR_ACCESS, GetSegmentAccessRights(tr));

    // LDTR
    USHORT ldtr = _readldtr();
    __vmx_vmwrite(VMCS_GUEST_LDTR_SEL, ldtr);
    __vmx_vmwrite(VMCS_GUEST_LDTR_BASE, GetSegmentBase(gdtr.Base, ldtr));
    __vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, __segmentlimit(ldtr));
    __vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS, ldtr ? GetSegmentAccessRights(ldtr) : 0x10000);

    // MSRs
    __vmx_vmwrite(VMCS_GUEST_DEBUGCTL, __readmsr(MSR_IA32_DEBUGCTL));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, __readmsr(MSR_IA32_SYSENTER_CS));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(MSR_IA32_SYSENTER_ESP));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(MSR_IA32_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_GUEST_EFER, __readmsr(MSR_IA32_EFER));
    __vmx_vmwrite(VMCS_GUEST_PAT, __readmsr(MSR_IA32_PAT));

    // Activity & interruptibility
    __vmx_vmwrite(VMCS_GUEST_ACTIVITY, 0);  // active
    __vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    // CR masks (no CR exits)
    __vmx_vmwrite(VMCS_CR0_GUEST_HOST_MASK, 0);
    __vmx_vmwrite(VMCS_CR4_GUEST_HOST_MASK, 0);
    __vmx_vmwrite(VMCS_CR0_READ_SHADOW, __readcr0());
    __vmx_vmwrite(VMCS_CR4_READ_SHADOW, __readcr4());
    __vmx_vmwrite(VMCS_CR3_TARGET_COUNT, 0);

    // Exception bitmap (0 = no exception exits)
    __vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);

    // --- Host state ---
    __vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
    __vmx_vmwrite(VMCS_HOST_CR3, __readcr3());
    __vmx_vmwrite(VMCS_HOST_CR4, __readcr4());

    __vmx_vmwrite(VMCS_HOST_CS_SEL, cs & 0xFFF8);
    __vmx_vmwrite(VMCS_HOST_SS_SEL, ss & 0xFFF8);
    __vmx_vmwrite(VMCS_HOST_DS_SEL, ds & 0xFFF8);
    __vmx_vmwrite(VMCS_HOST_ES_SEL, es & 0xFFF8);
    __vmx_vmwrite(VMCS_HOST_FS_SEL, fs & 0xFFF8);
    __vmx_vmwrite(VMCS_HOST_GS_SEL, gs & 0xFFF8);
    __vmx_vmwrite(VMCS_HOST_TR_SEL, tr & 0xFFF8);

    __vmx_vmwrite(VMCS_HOST_FS_BASE, __readmsr(MSR_FS_BASE));
    __vmx_vmwrite(VMCS_HOST_GS_BASE, __readmsr(MSR_GS_BASE));
    __vmx_vmwrite(VMCS_HOST_TR_BASE, GetSegmentBase(gdtr.Base, tr));
    __vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtr.Base);
    __vmx_vmwrite(VMCS_HOST_IDTR_BASE, idtr.Base);

    __vmx_vmwrite(VMCS_HOST_SYSENTER_CS, __readmsr(MSR_IA32_SYSENTER_CS));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(MSR_IA32_SYSENTER_ESP));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(MSR_IA32_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_HOST_EFER, __readmsr(MSR_IA32_EFER));
    __vmx_vmwrite(VMCS_HOST_PAT, __readmsr(MSR_IA32_PAT));

    __vmx_vmwrite(VMCS_HOST_RSP, cpu->HostStackTop);
    __vmx_vmwrite(VMCS_HOST_RIP, (ULONG64)AsmVmxEntryPoint);

    // Guest RIP/RSP will be set by AsmVmxLaunch right before VMLAUNCH
    // (it captures RSP and sets RIP to the instruction after VMLAUNCH)

    cpu->Virtualized = TRUE;
    HvLog("CPU %u: VMCS configured, launching...", CpuIndex);

    // Launch the VM (this function captures state and does VMLAUNCH)
    AsmVmxLaunch();

    // If we get here, VMLAUNCH succeeded and we returned via VMCALL exit
    HvLog("CPU %u: running under hypervisor", CpuIndex);

    KeRevertToUserGroupAffinityThread(&oldAffinity);
    return STATUS_SUCCESS;

fail_vmxoff:
    __vmx_off();
fail:
    KeRevertToUserGroupAffinityThread(&oldAffinity);
    return status;
}

// ── VM Exit Handler ──
// Called from assembly stub with a pointer to the CPU state.
// Returns TRUE to resume guest, FALSE to devirtualize.

#define EXIT_REASON_MTF 37

BOOLEAN VmxExitHandler(PVMX_CPU_STATE CpuState)
{
    ULONG64 exitReason;
    __vmx_vmread(VMCS_EXIT_REASON, &exitReason);
    exitReason &= 0xFFFF; // mask off entry-failure bit

    switch (exitReason) {

    case EXIT_REASON_EPT_VIOLATION: {
        ULONG64 qualification, guestPA;
        __vmx_vmread(VMCS_EXIT_QUALIFICATION, &qualification);
        __vmx_vmread(VMCS_GUEST_PHYSICAL_ADDRESS, &guestPA);

        if (EptHandleViolation(&CpuState->EptState, guestPA, qualification))
            return TRUE;

        // Unhandled EPT violation — should not happen with identity mapping
        HvLogError("Unhandled EPT violation: PA=0x%llX qual=0x%llX", guestPA, qualification);
        return TRUE; // resume anyway
    }

    case EXIT_REASON_MTF: {
        // Monitor Trap Flag — single step completed
        EptHandleMtfExit(&CpuState->EptState);
        return TRUE;
    }

    case EXIT_REASON_VMCALL: {
        // Advance RIP past VMCALL (3 bytes)
        ULONG64 rip, instrLen;
        __vmx_vmread(VMCS_GUEST_RIP, &rip);
        __vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN, &instrLen);
        __vmx_vmwrite(VMCS_GUEST_RIP, rip + instrLen);

        // Check if it's our devirtualize call
        // Convention: RCX in guest = hypercall ID
        // We can read guest RCX from the saved state
        // For now, VMCALL with RCX=0xDEAD means devirtualize
        // (handled in assembly stub which passes registers)
        return TRUE;
    }

    case EXIT_REASON_CPUID: {
        // Pass through CPUID but hide VMX support if needed
        ULONG64 rip, instrLen;
        __vmx_vmread(VMCS_GUEST_RIP, &rip);
        __vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN, &instrLen);
        __vmx_vmwrite(VMCS_GUEST_RIP, rip + instrLen);
        // CPUID result is handled by the assembly stub reading EAX from guest
        return TRUE;
    }

    case EXIT_REASON_XSETBV: {
        ULONG64 rip, instrLen;
        __vmx_vmread(VMCS_GUEST_RIP, &rip);
        __vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN, &instrLen);
        __vmx_vmwrite(VMCS_GUEST_RIP, rip + instrLen);
        return TRUE;
    }

    case EXIT_REASON_EPT_MISCONFIG: {
        ULONG64 guestPA;
        __vmx_vmread(VMCS_GUEST_PHYSICAL_ADDRESS, &guestPA);
        HvLogError("EPT misconfiguration at PA=0x%llX", guestPA);
        return TRUE;
    }

    default:
        HvLogError("Unhandled VM exit reason: %u", (ULONG)exitReason);
        return TRUE;
    }
}

// ── Devirtualize one CPU ──

void VmxDevirtualizeCpu(ULONG CpuIndex)
{
    PVMX_CPU_STATE cpu = &g_Hv.CpuStates[CpuIndex];
    if (!cpu->Virtualized)
        return;

    GROUP_AFFINITY oldAffinity, newAffinity = { 0 };
    newAffinity.Group = 0;
    newAffinity.Mask = (KAFFINITY)(1ULL << CpuIndex);
    KeSetSystemGroupAffinityThread(&newAffinity, &oldAffinity);

    // Issue VMCALL to devirtualize (handled by exit handler)
    AsmVmxVmCall(0xDEAD, 0, 0);

    cpu->Virtualized = FALSE;

    // Clean up EPT
    EptRemoveAllHooks(&cpu->EptState);
    EptDestroy(&cpu->EptState);

    KeRevertToUserGroupAffinityThread(&oldAffinity);
    HvLog("CPU %u devirtualized", CpuIndex);
}

// ── Terminate VMX ──

void VmxTerminate(void)
{
    if (!g_Hv.CpuStates)
        return;

    for (ULONG i = 0; i < g_Hv.CpuCount; i++) {
        VmxDevirtualizeCpu(i);

        PVMX_CPU_STATE cpu = &g_Hv.CpuStates[i];
        if (cpu->VmxonRegion)  ExFreePoolWithTag(cpu->VmxonRegion, 'xmVH');
        if (cpu->VmcsRegion)   ExFreePoolWithTag(cpu->VmcsRegion, 'xmVH');
        if (cpu->MsrBitmap)    ExFreePoolWithTag(cpu->MsrBitmap, 'xmVH');
        if (cpu->HostStack)    ExFreePoolWithTag(cpu->HostStack, 'kSvH');
    }

    ExFreePoolWithTag(g_Hv.CpuStates, 'tSvH');
    g_Hv.CpuStates = NULL;
    g_Hv.Running = FALSE;

    HvLog("VMX terminated");
}
