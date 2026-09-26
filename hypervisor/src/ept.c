#include "ept.h"
#include "log.h"

// ── Helpers ──

static ULONG64 VaToPA(PVOID Va)
{
    PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(Va);
    return pa.QuadPart;
}

// Allocate a zeroed, page-aligned, non-paged buffer
static PVOID EptAllocPage(void)
{
    PVOID p = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, 'tpEH');
    if (p)
        RtlZeroMemory(p, PAGE_SIZE);
    return p;
}

// ── Build identity-mapped EPT (2MB large pages) ──
// Maps the first 512 GB of physical memory 1:1.

NTSTATUS EptInitialize(PEPT_STATE EptState)
{
    InitializeListHead(&EptState->HookList);
    KeInitializeSpinLock(&EptState->HookLock);
    RtlZeroMemory(EptState->Pml4, PAGE_SIZE);

    // PML4 → 1 PDPT → 512 PDs → each with 512 * 2MB entries = 512 GB
    EPT_PTE* Pdpt = (EPT_PTE*)EptAllocPage();
    if (!Pdpt) return STATUS_INSUFFICIENT_RESOURCES;

    EptState->Pml4[0].Value = 0;
    EptState->Pml4[0].Read = 1;
    EptState->Pml4[0].Write = 1;
    EptState->Pml4[0].Execute = 1;
    EptState->Pml4[0].Pfn = VaToPA(Pdpt) >> 12;

    for (ULONG i = 0; i < EPT_ENTRY_COUNT; i++) {
        EPT_PTE* Pd = (EPT_PTE*)EptAllocPage();
        if (!Pd) return STATUS_INSUFFICIENT_RESOURCES;

        Pdpt[i].Value = 0;
        Pdpt[i].Read = 1;
        Pdpt[i].Write = 1;
        Pdpt[i].Execute = 1;
        Pdpt[i].Pfn = VaToPA(Pd) >> 12;

        for (ULONG j = 0; j < EPT_ENTRY_COUNT; j++) {
            ULONG64 pa = ((ULONG64)i * EPT_ENTRY_COUNT + j) * (2 * 1024 * 1024);
            Pd[j].Value = 0;
            Pd[j].Read = 1;
            Pd[j].Write = 1;
            Pd[j].Execute = 1;
            Pd[j].LargePage = 1;
            Pd[j].MemoryType = EPT_MEMORY_TYPE_WB;
            Pd[j].Pfn = pa >> 12;
        }
    }

    EptState->EptPointer.Value = 0;
    EptState->EptPointer.MemoryType = EPT_MEMORY_TYPE_WB;
    EptState->EptPointer.PageWalkLen = 3;   // 4-level walk
    EptState->EptPointer.Pfn = VaToPA(EptState->Pml4) >> 12;

    HvLog("EPT identity map built (512 GB, 2MB pages)");
    return STATUS_SUCCESS;
}

// ── Split a 2MB page into 4KB pages so we can hook one ──
// Returns pointer to the new PD entry that now points to a PT.

static PEPT_PTE EptSplit2MBPage(PEPT_STATE EptState, ULONG64 PhysicalAddress)
{
    ULONG64 pageBase = PhysicalAddress & ~((2ULL * 1024 * 1024) - 1);

    // Walk PML4 → PDPT → PD
    ULONG pml4Idx = (ULONG)((PhysicalAddress >> EPT_PML4_SHIFT) & 0x1FF);
    ULONG pdptIdx = (ULONG)((PhysicalAddress >> EPT_PDPT_SHIFT) & 0x1FF);
    ULONG pdIdx   = (ULONG)((PhysicalAddress >> EPT_PD_SHIFT)   & 0x1FF);

    EPT_PTE* Pdpt = (EPT_PTE*)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = (LONGLONG)(EptState->Pml4[pml4Idx].Pfn << 12) });
    if (!Pdpt) return NULL;

    EPT_PTE* Pd = (EPT_PTE*)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = (LONGLONG)(Pdpt[pdptIdx].Pfn << 12) });
    if (!Pd) return NULL;

    PEPT_PTE pde = &Pd[pdIdx];
    if (!pde->LargePage) {
        // Already split — find the PT entry
        EPT_PTE* Pt = (EPT_PTE*)MmGetVirtualForPhysical(
            (PHYSICAL_ADDRESS){ .QuadPart = (LONGLONG)(pde->Pfn << 12) });
        ULONG ptIdx = (ULONG)((PhysicalAddress >> EPT_PT_SHIFT) & 0x1FF);
        return &Pt[ptIdx];
    }

    // Allocate a new page table (512 * 4KB entries covering the same 2MB)
    EPT_PTE* Pt = (EPT_PTE*)EptAllocPage();
    if (!Pt) return NULL;

    for (ULONG i = 0; i < EPT_ENTRY_COUNT; i++) {
        Pt[i].Value = 0;
        Pt[i].Read = 1;
        Pt[i].Write = 1;
        Pt[i].Execute = 1;
        Pt[i].MemoryType = EPT_MEMORY_TYPE_WB;
        Pt[i].Pfn = (pageBase >> 12) + i;
    }

    // Update PDE: no longer large page, now points to PT
    pde->LargePage = 0;
    pde->Pfn = VaToPA(Pt) >> 12;
    // keep RWX on the PDE

    EptInvalidate();

    ULONG ptIdx = (ULONG)((PhysicalAddress >> EPT_PT_SHIFT) & 0x1FF);
    return &Pt[ptIdx];
}

// ── Install an EPT hook ──
// Creates a shadow page where the target function's bytes are replaced with
// a JMP to HookFunction. The EPT entry is set to execute-only pointing to
// the shadow page. Reads/writes see the original page.
// On EPT violation (read/write): temporarily swap to original, set MTF for
// single-step, then swap back to shadow on MTF exit.

NTSTATUS EptInstallHook(
    PEPT_STATE EptState,
    PVOID      TargetFunction,
    PVOID      HookFunction,
    PEPT_HOOK* OutHook)
{
    if (!MmIsAddressValid(TargetFunction))
        return STATUS_INVALID_ADDRESS;

    ULONG64 targetPA = VaToPA(TargetFunction);
    ULONG64 pageVA   = (ULONG64)TargetFunction & ~(PAGE_SIZE - 1);
    ULONG   offset   = (ULONG)((ULONG64)TargetFunction & (PAGE_SIZE - 1));

    // Split the 2MB page to 4KB so we can hook just this page
    PEPT_PTE pte = EptSplit2MBPage(EptState, targetPA);
    if (!pte)
        return STATUS_INSUFFICIENT_RESOURCES;

    // Allocate shadow page and copy original contents
    PVOID shadowPage = EptAllocPage();
    if (!shadowPage)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(shadowPage, (PVOID)pageVA, PAGE_SIZE);

    // Save original bytes
    PEPT_HOOK hook = (PEPT_HOOK)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(EPT_HOOK), 'kHvH');
    if (!hook) {
        ExFreePoolWithTag(shadowPage, 'tpEH');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(hook, sizeof(EPT_HOOK));
    hook->TargetVA      = (ULONG64)TargetFunction;
    hook->TargetPA      = targetPA;
    hook->OrigPagePfn   = pte->Pfn;
    hook->ShadowPage    = shadowPage;
    hook->ShadowPagePA  = VaToPA(shadowPage);
    hook->OffsetInPage  = offset;
    hook->EptEntry      = pte;
    hook->HookHandler   = HookFunction;
    RtlCopyMemory(hook->OriginalBytes, (PUCHAR)TargetFunction, 16);

    // Patch shadow page: write absolute JMP to hook handler
    // FF 25 00 00 00 00 [8-byte address] = 14 bytes
    PUCHAR patch = (PUCHAR)shadowPage + offset;
    patch[0] = 0xFF;
    patch[1] = 0x25;
    *(PULONG)(&patch[2]) = 0;  // RIP-relative offset = 0
    *(PULONG64)(&patch[6]) = (ULONG64)HookFunction;

    // Set EPT: execute → shadow page (execute-only)
    pte->Pfn = hook->ShadowPagePA >> 12;
    pte->Read = 0;
    pte->Write = 0;
    pte->Execute = 1;

    EptInvalidate();

    InsertTailList(&EptState->HookList, &hook->ListEntry);

    if (OutHook)
        *OutHook = hook;

    HvLog("EPT hook installed: target=0x%llX shadow=0x%llX handler=0x%llX",
          (ULONG64)TargetFunction, hook->ShadowPagePA, (ULONG64)HookFunction);

    return STATUS_SUCCESS;
}

// ── Handle EPT violation ──
// When read/write hits a hooked page (execute-only), temporarily swap to
// original page and enable Monitor Trap Flag for single-step.

BOOLEAN EptHandleViolation(
    PEPT_STATE EptState,
    ULONG64    GuestPhysical,
    ULONG64    Qualification)
{
    ULONG64 faultPagePA = GuestPhysical & ~(PAGE_SIZE - 1);

    PLIST_ENTRY entry = EptState->HookList.Flink;
    while (entry != &EptState->HookList) {
        PEPT_HOOK hook = CONTAINING_RECORD(entry, EPT_HOOK, ListEntry);
        ULONG64 shadowPagePA = hook->ShadowPagePA & ~(PAGE_SIZE - 1);
        ULONG64 origPagePA   = (hook->OrigPagePfn << 12) & ~(PAGE_SIZE - 1);

        if (faultPagePA == shadowPagePA || faultPagePA == origPagePA) {
            BOOLEAN isExec  = (Qualification & EPT_VIOLATION_EXECUTE) != 0;
            BOOLEAN isRead  = (Qualification & EPT_VIOLATION_READ)    != 0;
            BOOLEAN isWrite = (Qualification & EPT_VIOLATION_WRITE)   != 0;

            if (isRead || isWrite) {
                // Read/write on execute-only shadow page → show original
                hook->EptEntry->Pfn = hook->OrigPagePfn;
                hook->EptEntry->Read = 1;
                hook->EptEntry->Write = 1;
                hook->EptEntry->Execute = 0;
                hook->SingleStepping = TRUE;
                EptInvalidate();

                // Enable MTF (Monitor Trap Flag) for single-step
                ULONG64 procCtls;
                __vmx_vmread(VMCS_PROC_BASED_CONTROLS, &procCtls);
                procCtls |= (1UL << 27); // MTF
                __vmx_vmwrite(VMCS_PROC_BASED_CONTROLS, procCtls);

                return TRUE;
            }

            if (isExec) {
                // Execution on read/write-only page (after single-step)
                // → swap back to shadow (execute-only)
                hook->EptEntry->Pfn = hook->ShadowPagePA >> 12;
                hook->EptEntry->Read = 0;
                hook->EptEntry->Write = 0;
                hook->EptEntry->Execute = 1;
                hook->SingleStepping = FALSE;
                EptInvalidate();
                return TRUE;
            }
        }
        entry = entry->Flink;
    }
    return FALSE;
}

// ── Handle MTF exit (single-step complete) → restore hook ──
void EptHandleMtfExit(PEPT_STATE EptState)
{
    // Disable MTF
    ULONG64 procCtls;
    __vmx_vmread(VMCS_PROC_BASED_CONTROLS, &procCtls);
    procCtls &= ~(1ULL << 27);
    __vmx_vmwrite(VMCS_PROC_BASED_CONTROLS, procCtls);

    // Restore all hooks that were single-stepping
    PLIST_ENTRY entry = EptState->HookList.Flink;
    while (entry != &EptState->HookList) {
        PEPT_HOOK hook = CONTAINING_RECORD(entry, EPT_HOOK, ListEntry);
        if (hook->SingleStepping) {
            hook->EptEntry->Pfn = hook->ShadowPagePA >> 12;
            hook->EptEntry->Read = 0;
            hook->EptEntry->Write = 0;
            hook->EptEntry->Execute = 1;
            hook->SingleStepping = FALSE;
        }
        entry = entry->Flink;
    }
    EptInvalidate();
}

// ── Remove all hooks ──
void EptRemoveAllHooks(PEPT_STATE EptState)
{
    while (!IsListEmpty(&EptState->HookList)) {
        PLIST_ENTRY entry = RemoveHeadList(&EptState->HookList);
        PEPT_HOOK hook = CONTAINING_RECORD(entry, EPT_HOOK, ListEntry);

        // Restore original page in EPT
        hook->EptEntry->Pfn = hook->OrigPagePfn;
        hook->EptEntry->Read = 1;
        hook->EptEntry->Write = 1;
        hook->EptEntry->Execute = 1;

        if (hook->ShadowPage)
            ExFreePoolWithTag(hook->ShadowPage, 'tpEH');
        ExFreePoolWithTag(hook, 'kHvH');
    }
    EptInvalidate();
}

void EptDestroy(PEPT_STATE EptState)
{
    EptRemoveAllHooks(EptState);
    // Note: in production, also free all PT/PD/PDPT pages allocated
}

EPTP EptGetPointer(PEPT_STATE EptState)
{
    return EptState->EptPointer;
}

void EptInvalidate(void)
{
    // INVEPT type 1 = single-context invalidation
    // For simplicity we use type 2 = all-context
    struct { ULONG64 eptp; ULONG64 reserved; } desc = { 0, 0 };
    unsigned char result;

    // Type 2 = global invalidation
    result = __invept(2, &desc);
    if (result != 0) {
        HvLogError("INVEPT failed with result %d", result);
    }
}
