#pragma once
#include "vmx.h"

// ============================================================
// EPT (Extended Page Tables) definitions and hook structures
// ============================================================

#define EPT_PML4E_COUNT   512
#define EPT_PDPTE_COUNT   512
#define EPT_PDE_COUNT     512
#define EPT_PTE_COUNT     512

#define PAGE_SIZE_4KB     0x1000ULL
#define PAGE_SIZE_2MB     0x200000ULL
#define PAGE_MASK_4KB     (~(PAGE_SIZE_4KB - 1))

typedef union _EPT_PML4E {
    struct {
        UINT64 Read         : 1;
        UINT64 Write        : 1;
        UINT64 Execute      : 1;
        UINT64 Reserved1    : 5;
        UINT64 Accessed     : 1;
        UINT64 Ignored1     : 1;
        UINT64 UserMode     : 1;
        UINT64 Ignored2     : 1;
        UINT64 PhysAddr     : 40;
        UINT64 Ignored3     : 12;
    } Fields;
    UINT64 Value;
} EPT_PML4E, *PEPT_PML4E;

typedef union _EPT_PDPTE {
    struct {
        UINT64 Read         : 1;
        UINT64 Write        : 1;
        UINT64 Execute      : 1;
        UINT64 Reserved1    : 5;
        UINT64 Accessed     : 1;
        UINT64 Ignored1     : 1;
        UINT64 UserMode     : 1;
        UINT64 Ignored2     : 1;
        UINT64 PhysAddr     : 40;
        UINT64 Ignored3     : 12;
    } Fields;
    UINT64 Value;
} EPT_PDPTE, *PEPT_PDPTE;

typedef union _EPT_PDE {
    struct {
        UINT64 Read         : 1;
        UINT64 Write        : 1;
        UINT64 Execute      : 1;
        UINT64 MemoryType   : 3;
        UINT64 IgnorePAT    : 1;
        UINT64 LargePage    : 1;
        UINT64 Accessed     : 1;
        UINT64 Dirty        : 1;
        UINT64 UserMode     : 1;
        UINT64 Ignored1     : 1;
        UINT64 PhysAddr     : 40;
        UINT64 Ignored2     : 12;
    } Fields;
    UINT64 Value;
} EPT_PDE, *PEPT_PDE;

typedef union _EPT_PTE {
    struct {
        UINT64 Read         : 1;
        UINT64 Write        : 1;
        UINT64 Execute      : 1;
        UINT64 MemoryType   : 3;
        UINT64 IgnorePAT    : 1;
        UINT64 Reserved1    : 1;
        UINT64 Accessed     : 1;
        UINT64 Dirty        : 1;
        UINT64 UserMode     : 1;
        UINT64 Ignored1     : 1;
        UINT64 PhysAddr     : 40;
        UINT64 Ignored2     : 12;
    } Fields;
    UINT64 Value;
} EPT_PTE, *PEPT_PTE;

typedef union _EPTP {
    struct {
        UINT64 MemoryType       : 3;
        UINT64 PageWalkLength   : 3;
        UINT64 DirtyAccess      : 1;
        UINT64 Reserved1        : 5;
        UINT64 PML4PhysAddr     : 40;
        UINT64 Reserved2        : 12;
    } Fields;
    UINT64 Value;
} EPTP, *PEPTP;

// Hook state for a single EPT-hooked page
typedef enum _EPT_HOOK_STATE {
    HOOK_INACTIVE = 0,
    HOOK_ACTIVE,
    HOOK_SINGLESTEP
} EPT_HOOK_STATE;

typedef struct _EPT_HOOK_ENTRY {
    UINT64          TargetVA;
    UINT64          TargetPA;
    UINT64          OrigPagePA;
    UINT64          HookPagePA;
    PVOID           OrigPageVA;
    PVOID           HookPageVA;
    UINT8           OriginalByte;
    EPT_HOOK_STATE  State;
    PEPT_PTE        PteEntry;
    BOOLEAN         IsRtlPcToFileHeader;
    BOOLEAN         IsMmGetSystemRoutineAddress;
} EPT_HOOK_ENTRY, *PEPT_HOOK_ENTRY;

#define MAX_EPT_HOOKS 8

typedef struct _EPT_STATE {
    EPTP            Eptp;
    PEPT_PML4E      PML4;
    EPT_HOOK_ENTRY  Hooks[MAX_EPT_HOOKS];
    UINT32          HookCount;
} EPT_STATE, *PEPT_STATE;
