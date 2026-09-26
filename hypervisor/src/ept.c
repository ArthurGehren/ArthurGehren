#include "../include/hv.h"

// ============================================================
// EPT — identity-mapped page tables + hook via execute-only split
// ============================================================

static PEPT_PML4E EptAllocTable(void)
{
    PHYSICAL_ADDRESS maxAddr;
    maxAddr.QuadPart = MAXULONG64;
    PVOID table = MmAllocateContiguousMemory(PAGE_SIZE, maxAddr);
    if (table)
        RtlZeroMemory(table, PAGE_SIZE);
    return (PEPT_PML4E)table;
}

NTSTATUS EptInit(PEPT_STATE Ept)
{
    Ept->PML4 = EptAllocTable();
    if (!Ept->PML4)
        return STATUS_INSUFFICIENT_RESOURCES;

    // Identitiy map first 512 GB using 2MB pages
    // PML4[0] -> one PDPT -> 512 PDs -> each 512 2MB entries
    PEPT_PDPTE pdpt = (PEPT_PDPTE)EptAllocTable();
    if (!pdpt) return STATUS_INSUFFICIENT_RESOURCES;

    Ept->PML4[0].Value = 0;
    Ept->PML4[0].Fields.Read = 1;
    Ept->PML4[0].Fields.Write = 1;
    Ept->PML4[0].Fields.Execute = 1;
    Ept->PML4[0].Fields.PhysAddr =
        MmGetPhysicalAddress(pdpt).QuadPart >> 12;

    for (UINT32 i = 0; i < EPT_PDPTE_COUNT; i++) {
        PEPT_PDE pd = (PEPT_PDE)EptAllocTable();
        if (!pd) return STATUS_INSUFFICIENT_RESOURCES;

        pdpt[i].Value = 0;
        pdpt[i].Fields.Read = 1;
        pdpt[i].Fields.Write = 1;
        pdpt[i].Fields.Execute = 1;
        pdpt[i].Fields.PhysAddr =
            MmGetPhysicalAddress(pd).QuadPart >> 12;

        for (UINT32 j = 0; j < EPT_PDE_COUNT; j++) {
            UINT64 physAddr = ((UINT64)i * EPT_PDPTE_COUNT + j) * PAGE_SIZE_2MB;
            pd[j].Value = 0;
            pd[j].Fields.Read = 1;
            pd[j].Fields.Write = 1;
            pd[j].Fields.Execute = 1;
            pd[j].Fields.LargePage = 1;
            pd[j].Fields.MemoryType = EPT_MEMORY_TYPE_WB;
            pd[j].Fields.PhysAddr = physAddr >> 12;
        }
    }

    Ept->Eptp.Value = 0;
    Ept->Eptp.Fields.MemoryType = EPT_MEMORY_TYPE_WB;
    Ept->Eptp.Fields.PageWalkLength = 3; // 4-level walk - 1
    Ept->Eptp.Fields.DirtyAccess = 0;
    Ept->Eptp.Fields.PML4PhysAddr =
        MmGetPhysicalAddress(Ept->PML4).QuadPart >> 12;

    Ept->HookCount = 0;
    return STATUS_SUCCESS;
}

// Split a 2MB page into 512 4KB pages so we can hook one
static PEPT_PTE EptSplitLargePage(PEPT_STATE Ept, UINT64 PhysAddr)
{
    UINT32 pml4Idx = (UINT32)((PhysAddr >> 39) & 0x1FF);
    UINT32 pdptIdx = (UINT32)((PhysAddr >> 30) & 0x1FF);
    UINT32 pdIdx   = (UINT32)((PhysAddr >> 21) & 0x1FF);

    if (pml4Idx != 0) return NULL; // only first 512GB mapped

    PEPT_PDPTE pdpt = (PEPT_PDPTE)(
        (UINT64)MmGetVirtualForPhysical(
            *(PPHYSICAL_ADDRESS)&(UINT64){Ept->PML4[pml4Idx].Fields.PhysAddr << 12}
        ));
    // Read the physical address from the PML4 entry properly
    PHYSICAL_ADDRESS pdptPA;
    pdptPA.QuadPart = Ept->PML4[pml4Idx].Fields.PhysAddr << 12;
    pdpt = (PEPT_PDPTE)MmGetVirtualForPhysical(pdptPA);
    if (!pdpt) return NULL;

    PHYSICAL_ADDRESS pdPA;
    pdPA.QuadPart = pdpt[pdptIdx].Fields.PhysAddr << 12;
    PEPT_PDE pd = (PEPT_PDE)MmGetVirtualForPhysical(pdPA);
    if (!pd) return NULL;

    if (!pd[pdIdx].Fields.LargePage) {
        // Already split — find the PTE
        PHYSICAL_ADDRESS ptPA;
        ptPA.QuadPart = pd[pdIdx].Fields.PhysAddr << 12;
        PEPT_PTE pt = (PEPT_PTE)MmGetVirtualForPhysical(ptPA);
        UINT32 pteIdx = (UINT32)((PhysAddr >> 12) & 0x1FF);
        return &pt[pteIdx];
    }

    // Allocate a new PT and fill with 4KB identity map
    PEPT_PTE pt = (PEPT_PTE)EptAllocTable();
    if (!pt) return NULL;

    UINT64 basePhys = (PhysAddr & ~(PAGE_SIZE_2MB - 1));
    for (UINT32 i = 0; i < EPT_PTE_COUNT; i++) {
        pt[i].Value = 0;
        pt[i].Fields.Read = 1;
        pt[i].Fields.Write = 1;
        pt[i].Fields.Execute = 1;
        pt[i].Fields.MemoryType = EPT_MEMORY_TYPE_WB;
        pt[i].Fields.PhysAddr = (basePhys + i * PAGE_SIZE_4KB) >> 12;
    }

    pd[pdIdx].Value = 0;
    pd[pdIdx].Fields.Read = 1;
    pd[pdIdx].Fields.Write = 1;
    pd[pdIdx].Fields.Execute = 1;
    pd[pdIdx].Fields.LargePage = 0;
    pd[pdIdx].Fields.PhysAddr =
        MmGetPhysicalAddress(pt).QuadPart >> 12;

    UINT32 pteIdx = (UINT32)((PhysAddr >> 12) & 0x1FF);
    return &pt[pteIdx];
}

NTSTATUS EptInstallHook(
    PEPT_STATE  Ept,
    UINT64      TargetVA,
    BOOLEAN     IsRtlPcToFileHeader,
    BOOLEAN     IsMmGetSystemRoutineAddress)
{
    if (Ept->HookCount >= MAX_EPT_HOOKS)
        return STATUS_INSUFFICIENT_RESOURCES;

    PHYSICAL_ADDRESS targetPA = MmGetPhysicalAddress((PVOID)TargetVA);
    if (targetPA.QuadPart == 0)
        return STATUS_INVALID_PARAMETER;

    PEPT_PTE pte = EptSplitLargePage(Ept, targetPA.QuadPart);
    if (!pte)
        return STATUS_INSUFFICIENT_RESOURCES;

    PEPT_HOOK_ENTRY hook = &Ept->Hooks[Ept->HookCount];
    hook->TargetVA = TargetVA;
    hook->TargetPA = targetPA.QuadPart;
    hook->PteEntry = pte;
    hook->IsRtlPcToFileHeader = IsRtlPcToFileHeader;
    hook->IsMmGetSystemRoutineAddress = IsMmGetSystemRoutineAddress;

    // Save original first byte and plant INT3 (0xCC) on the hooked copy
    hook->OriginalByte = *(PUINT8)TargetVA;

    // Remove execute permission, keep read/write — any exec attempt will VM-exit
    pte->Fields.Execute = 0;
    hook->State = HOOK_ACTIVE;

    Ept->HookCount++;

    // Invalidate EPT TLB (INVEPT)
    struct {
        UINT64 Eptp;
        UINT64 Reserved;
    } inveptDesc = { Ept->Eptp.Value, 0 };
    // Type 1 = single-context invalidation
    __invept(1, &inveptDesc);

    return STATUS_SUCCESS;
}

// Called from VM-exit handler on EPT violation
BOOLEAN EptHandleViolation(
    PEPT_STATE  Ept,
    UINT64      GuestPhysAddr,
    UINT64      GuestRip,
    PGUEST_REGS GuestRegs,
    PLOG_BUFFER Log)
{
    UINT64 faultPage = GuestPhysAddr & PAGE_MASK_4KB;

    for (UINT32 i = 0; i < Ept->HookCount; i++) {
        PEPT_HOOK_ENTRY hook = &Ept->Hooks[i];
        UINT64 hookPage = hook->TargetPA & PAGE_MASK_4KB;

        if (faultPage != hookPage)
            continue;

        if (hook->State == HOOK_SINGLESTEP) {
            // Re-enable the hook (remove exec again)
            hook->PteEntry->Fields.Execute = 0;
            hook->State = HOOK_ACTIVE;

            struct {
                UINT64 Eptp;
                UINT64 Reserved;
            } desc = { Ept->Eptp.Value, 0 };
            __invept(1, &desc);
            return TRUE;
        }

        // The guest tried to execute the hooked page — log it
        if (hook->State == HOOK_ACTIVE &&
            GuestPhysAddr == hook->TargetPA)
        {
            LOG_ENTRY entry = { 0 };
            KeQuerySystemTimePrecise(&entry.Timestamp);
            entry.ProcessId = (UINT32)(ULONG_PTR)PsGetCurrentProcessId();
            entry.ThreadId  = (UINT32)(ULONG_PTR)PsGetCurrentThreadId();
            entry.CallerRip = GuestRip;
            entry.ReturnAddress = *(PUINT64)GuestRegs->Rsp;

            if (hook->IsRtlPcToFileHeader) {
                entry.Type = LOG_RTLPC_TO_FILE_HEADER;
                entry.Detail.RtlPcToFileHeader.PcAddress = GuestRegs->Rcx;
            }
            else if (hook->IsMmGetSystemRoutineAddress) {
                entry.Type = LOG_MM_GET_SYSTEM_ROUTINE;
                // Rcx = pointer to UNICODE_STRING
                __try {
                    PUNICODE_STRING name = (PUNICODE_STRING)GuestRegs->Rcx;
                    USHORT copyLen = name->Length;
                    if (copyLen > sizeof(entry.Detail.MmGetSystemRoutine.RoutineName) - 2)
                        copyLen = sizeof(entry.Detail.MmGetSystemRoutine.RoutineName) - 2;
                    RtlCopyMemory(entry.Detail.MmGetSystemRoutine.RoutineName,
                                  name->Buffer, copyLen);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    RtlCopyMemory(entry.Detail.MmGetSystemRoutine.RoutineName,
                                  L"<fault>", sizeof(L"<fault>"));
                }
            }

            LogWrite(Log, &entry);

            // Single-step: restore exec, let one instruction run, then re-hook
            hook->PteEntry->Fields.Execute = 1;
            hook->State = HOOK_SINGLESTEP;

            struct {
                UINT64 Eptp;
                UINT64 Reserved;
            } desc = { Ept->Eptp.Value, 0 };
            __invept(1, &desc);

            // Set trap flag so we get a #DB after one instruction
            // (handled via MTF or the single-step mechanism)
            return TRUE;
        }

        // Read/write to the hooked exec-only page — just allow
        hook->PteEntry->Fields.Read = 1;
        hook->PteEntry->Fields.Write = 1;
        hook->State = HOOK_SINGLESTEP;

        struct {
            UINT64 Eptp;
            UINT64 Reserved;
        } desc = { Ept->Eptp.Value, 0 };
        __invept(1, &desc);
        return TRUE;
    }

    return FALSE; // not our hook
}
