#pragma once
#include <ntddk.h>

// ── EPT page table entry bits ──
#define EPT_READ        (1ULL << 0)
#define EPT_WRITE       (1ULL << 1)
#define EPT_EXECUTE     (1ULL << 2)
#define EPT_RWX         (EPT_READ | EPT_WRITE | EPT_EXECUTE)
#define EPT_MEMORY_TYPE_WB  6ULL

// Page walk levels
#define EPT_PML4_SHIFT  39
#define EPT_PDPT_SHIFT  30
#define EPT_PD_SHIFT    21
#define EPT_PT_SHIFT    12
#define EPT_ENTRY_COUNT 512

// Large page (2MB)
#define EPT_LARGE_PAGE  (1ULL << 7)

// ── EPT PTE (PML4E, PDPTE, PDE, PTE share this layout) ──
typedef union _EPT_PTE {
    ULONG64 Value;
    struct {
        ULONG64 Read        : 1;
        ULONG64 Write       : 1;
        ULONG64 Execute     : 1;
        ULONG64 MemoryType  : 3;   // only valid in leaf entries
        ULONG64 IgnorePat   : 1;
        ULONG64 LargePage   : 1;   // bit 7 - "maps a page" if set in PDE
        ULONG64 Accessed    : 1;
        ULONG64 Dirty       : 1;
        ULONG64 ExecuteUser : 1;
        ULONG64 Reserved1   : 1;
        ULONG64 Pfn         : 40;
        ULONG64 Reserved2   : 12;
    };
} EPT_PTE, *PEPT_PTE;

// ── EPT pointer (EPTP for VMCS) ──
typedef union _EPTP {
    ULONG64 Value;
    struct {
        ULONG64 MemoryType  : 3;   // 6 = WB
        ULONG64 PageWalkLen : 3;   // 3 = 4-level walk (value is walk_length - 1)
        ULONG64 DirtyAccess : 1;
        ULONG64 Reserved1   : 5;
        ULONG64 Pfn         : 40;
        ULONG64 Reserved2   : 12;
    };
} EPTP;

// ── EPT hook descriptor ──
typedef struct _EPT_HOOK {
    LIST_ENTRY  ListEntry;
    ULONG64     TargetVA;           // virtual address of hooked function
    ULONG64     TargetPA;           // physical address of hooked page
    ULONG64     OrigPagePfn;        // PFN of original physical page
    PVOID       ShadowPage;         // our shadow page with the hook trampoline
    ULONG64     ShadowPagePA;       // physical address of shadow page
    ULONG       OffsetInPage;       // offset of target function within the page
    PEPT_PTE    EptEntry;           // pointer to the EPT PTE for this page
    UCHAR       OriginalBytes[16];  // original bytes we overwrote with JMP
    PVOID       HookHandler;        // our detour function
    BOOLEAN     SingleStepping;     // currently single-stepping through original
} EPT_HOOK, *PEPT_HOOK;

// ── EPT state per-processor ──
typedef struct _EPT_STATE {
    DECLSPEC_ALIGN(PAGE_SIZE) EPT_PTE Pml4[EPT_ENTRY_COUNT];
    EPTP        EptPointer;
    LIST_ENTRY  HookList;
    KSPIN_LOCK  HookLock;
} EPT_STATE, *PEPT_STATE;

// ── API ──
NTSTATUS EptInitialize(PEPT_STATE EptState);
void     EptDestroy(PEPT_STATE EptState);
EPTP     EptGetPointer(PEPT_STATE EptState);

NTSTATUS EptInstallHook(
    PEPT_STATE EptState,
    PVOID      TargetFunction,
    PVOID      HookFunction,
    PEPT_HOOK* OutHook
);

void EptRemoveAllHooks(PEPT_STATE EptState);

BOOLEAN EptHandleViolation(
    PEPT_STATE EptState,
    ULONG64    GuestPhysical,
    ULONG64    Qualification
);

void EptInvalidate(void);
