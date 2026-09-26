#include "arch.h"
#include "util.h"
#include "debugaux.h"
#include "vmm.h"
#include "exit.h"
#include "lde64.h"
#include "hooklog.h"

// ============================================================
// Log buffer global — acessado pelos hooks e pelo driver IOCTL
// ============================================================
extern PHOOK_LOG_BUFFER g_HookLog;

// ============================================================
// Hook targets — original function pointers via trampoline
// ============================================================

typedef PVOID (*FN_RtlPcToFileHeader)(PVOID PcValue, PVOID* BaseOfImage);
FN_RtlPcToFileHeader RtlPcToFileHeaderOrig = NULL;

typedef PVOID (*FN_MmGetSystemRoutineAddress)(PUNICODE_STRING SystemRoutineName);
FN_MmGetSystemRoutineAddress MmGetSystemRoutineAddressOrig = NULL;

// ============================================================
// Hook: RtlPcToFileHeader
// ============================================================
PVOID RtlPcToFileHeaderHook(PVOID PcValue, PVOID* BaseOfImage)
{
	PVOID result = RtlPcToFileHeaderOrig(PcValue, BaseOfImage);

	if (g_HookLog) {
		HOOK_LOG_ENTRY entry;
		RtlZeroMemory(&entry, sizeof(entry));
		KeQuerySystemTimePrecise(&entry.Timestamp);
		entry.Type = HOOK_EVENT_RTLPC_TO_FILE_HEADER;
		entry.ProcessId = (UINT32)(ULONG_PTR)PsGetCurrentProcessId();
		entry.ThreadId  = (UINT32)(ULONG_PTR)PsGetCurrentThreadId();
		entry.Detail.RtlPcToFileHeader.PcAddress = (UINT64)PcValue;
		entry.Detail.RtlPcToFileHeader.ResolvedBase = (UINT64)(BaseOfImage ? *BaseOfImage : NULL);
		entry.CallerRip = (UINT64)_ReturnAddress();
		entry.ReturnAddress = entry.CallerRip;

		HookLogWrite(g_HookLog, &entry);
	}

	return result;
}

// ============================================================
// Hook: MmGetSystemRoutineAddress
// ============================================================
PVOID MmGetSystemRoutineAddressHook(PUNICODE_STRING SystemRoutineName)
{
	PVOID result = MmGetSystemRoutineAddressOrig(SystemRoutineName);

	if (g_HookLog) {
		HOOK_LOG_ENTRY entry;
		RtlZeroMemory(&entry, sizeof(entry));
		KeQuerySystemTimePrecise(&entry.Timestamp);
		entry.Type = HOOK_EVENT_MM_GET_SYSTEM_ROUTINE;
		entry.ProcessId = (UINT32)(ULONG_PTR)PsGetCurrentProcessId();
		entry.ThreadId  = (UINT32)(ULONG_PTR)PsGetCurrentThreadId();
		entry.CallerRip = (UINT64)_ReturnAddress();
		entry.ReturnAddress = entry.CallerRip;

		__try {
			if (SystemRoutineName && SystemRoutineName->Buffer && SystemRoutineName->Length > 0) {
				USHORT copyLen = SystemRoutineName->Length;
				if (copyLen > (MAX_ROUTINE_NAME - 1) * sizeof(WCHAR))
					copyLen = (MAX_ROUTINE_NAME - 1) * sizeof(WCHAR);
				RtlCopyMemory(entry.Detail.MmGetSystemRoutine.RoutineName,
					SystemRoutineName->Buffer, copyLen);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			RtlCopyMemory(entry.Detail.MmGetSystemRoutine.RoutineName,
				L"<fault>", sizeof(L"<fault>"));
		}

		HookLogWrite(g_HookLog, &entry);
	}

	return result;
}

// ============================================================
// Original gbhv EPT code below
// ============================================================

BOOL HvEptCheckFeatures()
{
	IA32_VMX_EPT_VPID_CAP_REGISTER VpidRegister;
	IA32_MTRR_DEF_TYPE_REGISTER MTRRDefType;

	VpidRegister.Flags = ArchGetHostMSR(IA32_VMX_EPT_VPID_CAP);
	MTRRDefType.Flags = ArchGetHostMSR(IA32_MTRR_DEF_TYPE);

	if (!VpidRegister.PageWalkLength4 || !VpidRegister.MemoryTypeWriteBack || !VpidRegister.Pde2MbPages)
		return FALSE;

	if (!VpidRegister.AdvancedVmexitEptViolationsInformation)
		HvUtilLogDebug("Processor does not support AdvancedVmexitEptViolationsInformation!\n");

	if (!MTRRDefType.MtrrEnable)
	{
		HvUtilLogError("MTRR Dynamic Ranges not supported.\n");
		return FALSE;
	}

	HvUtilLogSuccess("HvEptCheckFeatures: All EPT features present.\n");
	return TRUE;
}

BOOL HvEptBuildMTRRMap(PVMM_CONTEXT GlobalContext)
{
	IA32_MTRR_CAPABILITIES_REGISTER MTRRCap;
	IA32_MTRR_PHYSBASE_REGISTER CurrentPhysBase;
	IA32_MTRR_PHYSMASK_REGISTER CurrentPhysMask;
	PMTRR_RANGE_DESCRIPTOR Descriptor;
	ULONG CurrentRegister;
	ULONG NumberOfBitsInMask;

	MTRRCap.Flags = ArchGetHostMSR(IA32_MTRR_CAPABILITIES);

	for(CurrentRegister = 0; CurrentRegister < MTRRCap.VariableRangeCount; CurrentRegister++)
	{
		CurrentPhysBase.Flags = ArchGetHostMSR(IA32_MTRR_PHYSBASE0 + (CurrentRegister * 2));
		CurrentPhysMask.Flags = ArchGetHostMSR(IA32_MTRR_PHYSMASK0 + (CurrentRegister * 2));

		if(CurrentPhysMask.Valid)
		{
			Descriptor = &GlobalContext->MemoryRanges[GlobalContext->NumberOfEnabledMemoryRanges++];
			Descriptor->PhysicalBaseAddress = CurrentPhysBase.PageFrameNumber * PAGE_SIZE;
			_BitScanForward64(&NumberOfBitsInMask, CurrentPhysMask.PageFrameNumber * PAGE_SIZE);
			Descriptor->PhysicalEndAddress = Descriptor->PhysicalBaseAddress + ((1ULL << NumberOfBitsInMask) - 1ULL);
			Descriptor->MemoryType = (UCHAR) CurrentPhysBase.Type;
			if(Descriptor->MemoryType == MEMORY_TYPE_WRITE_BACK)
				GlobalContext->NumberOfEnabledMemoryRanges--;
		}
	}
	return TRUE;
}

VOID HvEptSetupPML2Entry(PVMM_CONTEXT GlobalContext, PEPT_PML2_ENTRY NewEntry, SIZE_T PageFrameNumber)
{
	SIZE_T AddressOfPage;
	SIZE_T CurrentMtrrRange;
	SIZE_T TargetMemoryType;

	NewEntry->PageFrameNumber = PageFrameNumber;
	AddressOfPage = PageFrameNumber * SIZE_2_MB;

	if(PageFrameNumber == 0)
	{
		NewEntry->MemoryType = MEMORY_TYPE_UNCACHEABLE;
		return;
	}

	TargetMemoryType = MEMORY_TYPE_WRITE_BACK;

	for(CurrentMtrrRange = 0; CurrentMtrrRange < GlobalContext->NumberOfEnabledMemoryRanges; CurrentMtrrRange++)
	{
		if(AddressOfPage <= GlobalContext->MemoryRanges[CurrentMtrrRange].PhysicalEndAddress)
		{
			if( (AddressOfPage + SIZE_2_MB - 1) >= GlobalContext->MemoryRanges[CurrentMtrrRange].PhysicalBaseAddress )
			{
				TargetMemoryType = GlobalContext->MemoryRanges[CurrentMtrrRange].MemoryType;
				if(TargetMemoryType == MEMORY_TYPE_UNCACHEABLE)
					break;
			}
		}
	}
	NewEntry->MemoryType = TargetMemoryType;
}

PVMM_EPT_PAGE_TABLE HvEptAllocateAndCreateIdentityPageTable(PVMM_CONTEXT GlobalContext)
{
	PVMM_EPT_PAGE_TABLE PageTable;
	EPT_PML3_POINTER RWXTemplate;
	EPT_PML2_ENTRY PML2EntryTemplate;
	SIZE_T EntryGroupIndex;
	SIZE_T EntryIndex;

	PageTable = OsAllocateContiguousAlignedPages(sizeof(VMM_EPT_PAGE_TABLE) / PAGE_SIZE);
	if(PageTable == NULL)
		return NULL;

	OsZeroMemory(PageTable, sizeof(VMM_EPT_PAGE_TABLE));
	InitializeListHead(&PageTable->DynamicSplitList);
	InitializeListHead(&PageTable->PageHookList);

	PageTable->PML4[0].PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML3[0]) / PAGE_SIZE;
	PageTable->PML4[0].ReadAccess = 1;
	PageTable->PML4[0].WriteAccess = 1;
	PageTable->PML4[0].ExecuteAccess = 1;

	RWXTemplate.Flags = 0;
	RWXTemplate.ReadAccess = 1;
	RWXTemplate.WriteAccess = 1;
	RWXTemplate.ExecuteAccess = 1;
	__stosq((SIZE_T*)&PageTable->PML3[0], RWXTemplate.Flags, VMM_EPT_PML3E_COUNT);

	for(EntryIndex = 0; EntryIndex < VMM_EPT_PML3E_COUNT; EntryIndex++)
		PageTable->PML3[EntryIndex].PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML2[EntryIndex][0]) / PAGE_SIZE;

	PML2EntryTemplate.Flags = 0;
	PML2EntryTemplate.WriteAccess = 1;
	PML2EntryTemplate.ReadAccess = 1;
	PML2EntryTemplate.ExecuteAccess = 1;
	PML2EntryTemplate.LargePage = 1;
	__stosq((SIZE_T*)&PageTable->PML2[0], PML2EntryTemplate.Flags, VMM_EPT_PML3E_COUNT * VMM_EPT_PML2E_COUNT);

	for(EntryGroupIndex = 0; EntryGroupIndex < VMM_EPT_PML3E_COUNT; EntryGroupIndex++)
		for(EntryIndex = 0; EntryIndex < VMM_EPT_PML2E_COUNT; EntryIndex++)
			HvEptSetupPML2Entry(GlobalContext, &PageTable->PML2[EntryGroupIndex][EntryIndex], (EntryGroupIndex * VMM_EPT_PML2E_COUNT) + EntryIndex);

	return PageTable;
}

BOOL HvEptGlobalInitialize(PVMM_CONTEXT GlobalContext)
{
	if (!HvEptCheckFeatures()) return FALSE;
	if(!HvEptBuildMTRRMap(GlobalContext)) return FALSE;
	return TRUE;
}

PEPT_PML2_ENTRY HvEptGetPml2Entry(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	SIZE_T Directory  = ADDRMASK_EPT_PML2_INDEX(PhysicalAddress);
	SIZE_T DirectoryPointer = ADDRMASK_EPT_PML3_INDEX(PhysicalAddress);
	SIZE_T PML4Entry = ADDRMASK_EPT_PML4_INDEX(PhysicalAddress);
	if(PML4Entry > 0) return NULL;
	return &ProcessorContext->EptPageTable->PML2[DirectoryPointer][Directory];
}

PEPT_PML1_ENTRY HvEptGetPml1Entry(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	SIZE_T Directory = ADDRMASK_EPT_PML2_INDEX(PhysicalAddress);
	SIZE_T DirectoryPointer = ADDRMASK_EPT_PML3_INDEX(PhysicalAddress);
	SIZE_T PML4Entry = ADDRMASK_EPT_PML4_INDEX(PhysicalAddress);
	if (PML4Entry > 0) return NULL;

	PEPT_PML2_ENTRY PML2 = &ProcessorContext->EptPageTable->PML2[DirectoryPointer][Directory];
	if(PML2->LargePage) return NULL;

	PEPT_PML2_POINTER PML2Pointer = (PEPT_PML2_POINTER) PML2;
	PEPT_PML1_ENTRY PML1 = (PEPT_PML1_ENTRY) OsPhysicalToVirtual((PPHYSVOID)(PML2Pointer->PageFrameNumber * PAGE_SIZE));
	if(!PML1) return NULL;
	return &PML1[ADDRMASK_EPT_PML1_INDEX(PhysicalAddress)];
}

BOOL HvEptSplitLargePage(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	PVMM_EPT_DYNAMIC_SPLIT NewSplit;
	EPT_PML1_ENTRY EntryTemplate;
	SIZE_T EntryIndex;
	PEPT_PML2_ENTRY TargetEntry;
	EPT_PML2_POINTER NewPointer;

	TargetEntry = HvEptGetPml2Entry(ProcessorContext, PhysicalAddress);
	if(!TargetEntry) return FALSE;
	if(!TargetEntry->LargePage) return TRUE;

	NewSplit = (PVMM_EPT_DYNAMIC_SPLIT) OsAllocateContiguousAlignedPages(sizeof(VMM_EPT_DYNAMIC_SPLIT)/PAGE_SIZE);
	if(!NewSplit) return FALSE;

	NewSplit->Entry = TargetEntry;
	EntryTemplate.Flags = 0;
	EntryTemplate.ReadAccess = 1;
	EntryTemplate.WriteAccess = 1;
	EntryTemplate.ExecuteAccess = 1;
	EntryTemplate.MemoryType = TargetEntry->MemoryType;
	EntryTemplate.IgnorePat = TargetEntry->IgnorePat;
	EntryTemplate.SuppressVe = TargetEntry->SuppressVe;
	__stosq((SIZE_T*)&NewSplit->PML1[0], EntryTemplate.Flags, VMM_EPT_PML1E_COUNT);

	for(EntryIndex = 0; EntryIndex < VMM_EPT_PML1E_COUNT; EntryIndex++)
		NewSplit->PML1[EntryIndex].PageFrameNumber = ( (TargetEntry->PageFrameNumber * SIZE_2_MB) / PAGE_SIZE ) + EntryIndex;

	NewPointer.Flags = 0;
	NewPointer.WriteAccess = 1;
	NewPointer.ReadAccess = 1;
	NewPointer.ExecuteAccess = 1;
	NewPointer.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&NewSplit->PML1[0]) / PAGE_SIZE;

	InsertHeadList(&ProcessorContext->EptPageTable->DynamicSplitList, &NewSplit->DynamicSplitList);
	RtlCopyMemory(TargetEntry, &NewPointer, sizeof(NewPointer));
	return TRUE;
}

VOID HvEptHookWriteAbsoluteJump(PCHAR TargetBuffer, SIZE_T TargetAddress)
{
	UINT32 Low32  = (UINT32)TargetAddress;
	UINT32 High32 = (UINT32)(TargetAddress >> 32);
	TargetBuffer[0] = 0x68;
	*((UINT32*)&TargetBuffer[1]) = Low32;
	*((UINT32*)&TargetBuffer[5]) = 0x042444C7;
	*((UINT32*)&TargetBuffer[9]) = High32;
	TargetBuffer[13] = 0xC3;
}

BOOL HvEptHookInstructionMemory(PVMM_EPT_PAGE_HOOK Hook, PVOID TargetFunction, PVOID HookFunction, PVOID* OrigFunction)
{
	SIZE_T SizeOfHookedInstructions;
	SIZE_T OffsetIntoPage = ADDRMASK_EPT_PML1_OFFSET((SIZE_T)TargetFunction);
	if ((OffsetIntoPage + 14) > PAGE_SIZE-1) return FALSE;

	for(SizeOfHookedInstructions = 0; SizeOfHookedInstructions < 14;
		SizeOfHookedInstructions += LDE((PCHAR)TargetFunction + SizeOfHookedInstructions, 64)) {}

	Hook->Trampoline = OsAllocateExecutableNonpagedMemory(SizeOfHookedInstructions + 14);
	if (!Hook->Trampoline) return FALSE;

	RtlCopyMemory(Hook->Trampoline, TargetFunction, SizeOfHookedInstructions);
	HvEptHookWriteAbsoluteJump(&Hook->Trampoline[SizeOfHookedInstructions], (SIZE_T)TargetFunction + SizeOfHookedInstructions);
	*OrigFunction = Hook->Trampoline;
	HvEptHookWriteAbsoluteJump(&Hook->FakePage[OffsetIntoPage], (SIZE_T)HookFunction);
	return TRUE;
}

BOOL HvEptAddPageHook(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVOID TargetFunction, PVOID HookFunction, PVOID* OrigFunction)
{
	PVMM_EPT_PAGE_HOOK NewHook;
	EPT_PML1_ENTRY OriginalEntry;
	INVEPT_DESCRIPTOR Descriptor;
	SIZE_T PhysicalAddress;
	PVOID VirtualTarget;

	VirtualTarget = PAGE_ALIGN(TargetFunction);
	PhysicalAddress = (SIZE_T) OsVirtualToPhysical(VirtualTarget);
	if(!PhysicalAddress) return FALSE;

	NewHook = (PVMM_EPT_PAGE_HOOK) OsAllocateNonpagedMemory(sizeof(VMM_EPT_PAGE_HOOK));
	if (!NewHook) return FALSE;

	if (!HvEptSplitLargePage(ProcessorContext, PhysicalAddress))
	{
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	OsZeroMemory(NewHook, sizeof(VMM_EPT_PAGE_HOOK));
	RtlCopyMemory(&NewHook->FakePage[0], VirtualTarget, PAGE_SIZE);
	NewHook->PhysicalBaseAddress = (SIZE_T) PAGE_ALIGN(PhysicalAddress);
	NewHook->TargetPage = HvEptGetPml1Entry(ProcessorContext, PhysicalAddress);
	if (!NewHook->TargetPage) { OsFreeNonpagedMemory(NewHook); return FALSE; }

	NewHook->OriginalEntry = *NewHook->TargetPage;
	OriginalEntry = *NewHook->TargetPage;

	EPT_PML1_ENTRY FakeEntry;
	FakeEntry.Flags = 0;
	FakeEntry.ReadAccess = 0;
	FakeEntry.WriteAccess = 0;
	FakeEntry.ExecuteAccess = 1;
	FakeEntry.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&NewHook->FakePage) / PAGE_SIZE;
	NewHook->ShadowEntry.Flags = FakeEntry.Flags;

	InsertHeadList(&ProcessorContext->EptPageTable->PageHookList, &NewHook->PageHookList);

	OriginalEntry.ReadAccess = 1;
	OriginalEntry.WriteAccess = 1;
	OriginalEntry.ExecuteAccess = 0;
	NewHook->HookedEntry.Flags = OriginalEntry.Flags;

	if(!HvEptHookInstructionMemory(NewHook, TargetFunction, HookFunction, OrigFunction))
	{
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	NewHook->TargetPage->Flags = OriginalEntry.Flags;

	if (ProcessorContext->HasLaunched)
	{
		Descriptor.EptPointer = ProcessorContext->EptPointer.Flags;
		Descriptor.Reserved = 0;
		__invept(1, &Descriptor);
	}
	return TRUE;
}

BOOL HvExitHandlePageHookExit(
	PVMM_PROCESSOR_CONTEXT ProcessorContext,
	PVMEXIT_CONTEXT ExitContext,
	VMX_EXIT_QUALIFICATION_EPT_VIOLATION ViolationQualification)
{
	PVMM_EPT_PAGE_HOOK PageHook = NULL;

	if (!ViolationQualification.CausedByTranslation) return FALSE;

	FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, PageHookList, VMM_EPT_PAGE_HOOK, Hook)
		if ( Hook->PhysicalBaseAddress == (SIZE_T)PAGE_ALIGN(ExitContext->GuestPhysicalAddress) )
		{ PageHook = Hook; break; }
	FOR_EACH_LIST_ENTRY_END();

	if (!PageHook) return FALSE;

	if(!ViolationQualification.EptExecutable && ViolationQualification.ExecuteAccess)
	{
		PageHook->TargetPage->Flags = PageHook->ShadowEntry.Flags;
		ExitContext->ShouldIncrementRIP = FALSE;
		return TRUE;
	}

	if(ViolationQualification.EptExecutable
		&& (ViolationQualification.ReadAccess | ViolationQualification.WriteAccess))
	{
		PageHook->TargetPage->Flags = PageHook->HookedEntry.Flags;
		ExitContext->ShouldIncrementRIP = FALSE;
		return TRUE;
	}
	return FALSE;
}

VOID HvExitHandleEptViolation(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	VMX_EXIT_QUALIFICATION_EPT_VIOLATION ViolationQualification;
	ViolationQualification.Flags = ExitContext->ExitQualification;

	if(HvExitHandlePageHookExit(ProcessorContext, ExitContext, ViolationQualification))
		return;

	HvUtilLogError("Unexpected EPT violation!\n");
	ExitContext->ShouldStopExecution = TRUE;
}

// ============================================================
// EPT per-processor init — instala os dois hooks
// ============================================================
BOOL HvEptLogicalProcessorInitialize(PVMM_PROCESSOR_CONTEXT ProcessorContext)
{
	PVMM_EPT_PAGE_TABLE PageTable;
	EPT_POINTER EPTP;

	PageTable = HvEptAllocateAndCreateIdentityPageTable(ProcessorContext->GlobalContext);
	if (PageTable == NULL) return FALSE;

	ProcessorContext->EptPageTable = PageTable;
	EPTP.Flags = 0;
	EPTP.MemoryType = MEMORY_TYPE_WRITE_BACK;
	EPTP.EnableAccessAndDirtyFlags = FALSE;
	EPTP.PageWalkLength = 3;
	EPTP.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML4) / PAGE_SIZE;
	ProcessorContext->EptPointer.Flags = EPTP.Flags;

	// Hook 1: RtlPcToFileHeader
	{
		UNICODE_STRING name = RTL_CONSTANT_STRING(L"RtlPcToFileHeader");
		PVOID target = MmGetSystemRoutineAddress(&name);
		if (target) {
			if (!HvEptAddPageHook(ProcessorContext, target,
				(PVOID)RtlPcToFileHeaderHook, (PVOID*)&RtlPcToFileHeaderOrig))
			{
				HvUtilLogError("[HV] Failed to hook RtlPcToFileHeader\n");
				HvEptFreeLogicalProcessorContext(ProcessorContext);
				return FALSE;
			}
			HvUtilLogSuccess("[HV] Hooked RtlPcToFileHeader @ 0x%llX\n", (SIZE_T)target);
		}
	}

	// Hook 2: MmGetSystemRoutineAddress
	{
		UNICODE_STRING name = RTL_CONSTANT_STRING(L"MmGetSystemRoutineAddress");
		PVOID target = MmGetSystemRoutineAddress(&name);
		if (target) {
			if (!HvEptAddPageHook(ProcessorContext, target,
				(PVOID)MmGetSystemRoutineAddressHook, (PVOID*)&MmGetSystemRoutineAddressOrig))
			{
				HvUtilLogError("[HV] Failed to hook MmGetSystemRoutineAddress\n");
				HvEptFreeLogicalProcessorContext(ProcessorContext);
				return FALSE;
			}
			HvUtilLogSuccess("[HV] Hooked MmGetSystemRoutineAddress @ 0x%llX\n", (SIZE_T)target);
		}
	}

	return TRUE;
}

VOID HvEptFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT ProcessorContext)
{
	if (ProcessorContext->EptPageTable)
	{
		FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, DynamicSplitList, VMM_EPT_DYNAMIC_SPLIT, Split)
			OsFreeNonpagedMemory(Split);
		FOR_EACH_LIST_ENTRY_END();

		FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, PageHookList, VMM_EPT_PAGE_HOOK, Hook)
			OsFreeNonpagedMemory(Hook->Trampoline);
			OsFreeNonpagedMemory(Hook);
		FOR_EACH_LIST_ENTRY_END();

		OsFreeContiguousAlignedPages(ProcessorContext->EptPageTable);
	}
}
