#pragma once
#include <ntddk.h>
#include <intrin.h>

// ============================================================
// Intel VT-x / VMX definitions
// ============================================================

#define VMX_OK              0
#define VMX_FAIL_INVALID    1
#define VMX_FAIL_VALID      2

// MSRs
#define IA32_VMX_BASIC              0x480
#define IA32_VMX_CR0_FIXED0         0x486
#define IA32_VMX_CR0_FIXED1         0x487
#define IA32_VMX_CR4_FIXED0         0x488
#define IA32_VMX_CR4_FIXED1         0x489
#define IA32_VMX_PINBASED_CTLS      0x481
#define IA32_VMX_PROCBASED_CTLS     0x482
#define IA32_VMX_PROCBASED_CTLS2    0x48B
#define IA32_VMX_EXIT_CTLS          0x483
#define IA32_VMX_ENTRY_CTLS         0x484
#define IA32_VMX_TRUE_PINBASED_CTLS 0x48D
#define IA32_VMX_TRUE_PROCBASED_CTLS 0x48E
#define IA32_VMX_TRUE_EXIT_CTLS     0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS    0x490
#define IA32_VMX_EPT_VPID_CAP      0x48C
#define IA32_FEATURE_CONTROL        0x03A
#define IA32_DEBUGCTL               0x1D9

// CR4 bits
#define CR4_VMXE  (1ULL << 13)

// VMCS field encodings
#define VMCS_GUEST_ES_SELECTOR          0x0800
#define VMCS_GUEST_CS_SELECTOR          0x0802
#define VMCS_GUEST_SS_SELECTOR          0x0804
#define VMCS_GUEST_DS_SELECTOR          0x0806
#define VMCS_GUEST_FS_SELECTOR          0x0808
#define VMCS_GUEST_GS_SELECTOR          0x080A
#define VMCS_GUEST_LDTR_SELECTOR        0x080C
#define VMCS_GUEST_TR_SELECTOR          0x080E

#define VMCS_GUEST_CR0                  0x6800
#define VMCS_GUEST_CR3                  0x6802
#define VMCS_GUEST_CR4                  0x6804
#define VMCS_GUEST_DR7                  0x681A
#define VMCS_GUEST_RSP                  0x681C
#define VMCS_GUEST_RIP                  0x681E
#define VMCS_GUEST_RFLAGS               0x6820
#define VMCS_GUEST_GDTR_BASE            0x6816
#define VMCS_GUEST_IDTR_BASE            0x6818
#define VMCS_GUEST_GDTR_LIMIT           0x4810
#define VMCS_GUEST_IDTR_LIMIT           0x4812

#define VMCS_GUEST_CS_LIMIT             0x4802
#define VMCS_GUEST_CS_ACCESS            0x4816
#define VMCS_GUEST_CS_BASE              0x6808
#define VMCS_GUEST_SS_LIMIT             0x4804
#define VMCS_GUEST_SS_ACCESS            0x4818
#define VMCS_GUEST_SS_BASE              0x680A
#define VMCS_GUEST_DS_LIMIT             0x4806
#define VMCS_GUEST_DS_ACCESS            0x481A
#define VMCS_GUEST_DS_BASE              0x680C
#define VMCS_GUEST_ES_LIMIT             0x4800
#define VMCS_GUEST_ES_ACCESS            0x4814
#define VMCS_GUEST_ES_BASE              0x6806
#define VMCS_GUEST_FS_LIMIT             0x4808
#define VMCS_GUEST_FS_ACCESS            0x481C
#define VMCS_GUEST_FS_BASE              0x680E
#define VMCS_GUEST_GS_LIMIT             0x480A
#define VMCS_GUEST_GS_ACCESS            0x481E
#define VMCS_GUEST_GS_BASE              0x6810
#define VMCS_GUEST_LDTR_LIMIT           0x480C
#define VMCS_GUEST_LDTR_ACCESS          0x4820
#define VMCS_GUEST_LDTR_BASE            0x6812
#define VMCS_GUEST_TR_LIMIT             0x480E
#define VMCS_GUEST_TR_ACCESS            0x4822
#define VMCS_GUEST_TR_BASE              0x6814

#define VMCS_GUEST_IA32_DEBUGCTL        0x2802
#define VMCS_GUEST_IA32_DEBUGCTL_HIGH   0x2803
#define VMCS_GUEST_SYSENTER_CS          0x482A
#define VMCS_GUEST_SYSENTER_ESP         0x6824
#define VMCS_GUEST_SYSENTER_EIP         0x6826
#define VMCS_GUEST_VMCS_LINK_PTR        0x2800
#define VMCS_GUEST_VMCS_LINK_PTR_HIGH   0x2801

#define VMCS_HOST_ES_SELECTOR           0x0C00
#define VMCS_HOST_CS_SELECTOR           0x0C02
#define VMCS_HOST_SS_SELECTOR           0x0C04
#define VMCS_HOST_DS_SELECTOR           0x0C06
#define VMCS_HOST_FS_SELECTOR           0x0C08
#define VMCS_HOST_GS_SELECTOR           0x0C0A
#define VMCS_HOST_TR_SELECTOR           0x0C0C
#define VMCS_HOST_CR0                   0x6C00
#define VMCS_HOST_CR3                   0x6C02
#define VMCS_HOST_CR4                   0x6C04
#define VMCS_HOST_RSP                   0x6C14
#define VMCS_HOST_RIP                   0x6C16
#define VMCS_HOST_GDTR_BASE             0x6C0C
#define VMCS_HOST_IDTR_BASE             0x6C0E
#define VMCS_HOST_FS_BASE               0x6C06
#define VMCS_HOST_GS_BASE               0x6C08
#define VMCS_HOST_TR_BASE               0x6C0A
#define VMCS_HOST_SYSENTER_CS           0x4C00
#define VMCS_HOST_SYSENTER_ESP          0x6C10
#define VMCS_HOST_SYSENTER_EIP          0x6C12

#define VMCS_CTRL_PIN_BASED             0x4000
#define VMCS_CTRL_PROC_BASED            0x4002
#define VMCS_CTRL_PROC_BASED2           0x401E
#define VMCS_CTRL_EXIT                  0x400C
#define VMCS_CTRL_ENTRY                 0x4012
#define VMCS_CTRL_EXCEPTION_BITMAP      0x4004
#define VMCS_CTRL_CR0_MASK              0x6000
#define VMCS_CTRL_CR4_MASK              0x6002
#define VMCS_CTRL_CR0_SHADOW            0x6004
#define VMCS_CTRL_CR4_SHADOW            0x6006
#define VMCS_CTRL_EPT_POINTER           0x201A
#define VMCS_CTRL_EPT_POINTER_HIGH      0x201B

#define VMCS_EXIT_REASON                0x4402
#define VMCS_EXIT_QUALIFICATION         0x6400
#define VMCS_EXIT_GUEST_LINEAR_ADDR     0x640A
#define VMCS_EXIT_GUEST_PHYSICAL_ADDR   0x2400
#define VMCS_EXIT_INSTRUCTION_LEN       0x440C
#define VMCS_EXIT_INSTRUCTION_INFO      0x440E
#define VMCS_EXIT_INTERRUPTION_INFO     0x4404
#define VMCS_EXIT_INTERRUPTION_ERROR    0x4406

// VM-exit reasons
#define EXIT_REASON_EXCEPTION_NMI       0
#define EXIT_REASON_EXTERNAL_INT        1
#define EXIT_REASON_TRIPLE_FAULT        2
#define EXIT_REASON_CPUID               10
#define EXIT_REASON_HLT                 12
#define EXIT_REASON_INVD                13
#define EXIT_REASON_VMCALL              18
#define EXIT_REASON_CR_ACCESS           28
#define EXIT_REASON_MSR_READ            31
#define EXIT_REASON_MSR_WRITE           32
#define EXIT_REASON_EPT_VIOLATION       48
#define EXIT_REASON_EPT_MISCONFIG       49
#define EXIT_REASON_XSETBV              55

// Proc-based controls
#define PROC_BASED_ACTIVATE_SECONDARY   (1UL << 31)
#define PROC_BASED2_ENABLE_EPT          (1UL << 1)
#define PROC_BASED2_ENABLE_VPID         (1UL << 5)
#define PROC_BASED2_UNRESTRICTED_GUEST  (1UL << 7)

// EPT memory types
#define EPT_MEMORY_TYPE_UC  0
#define EPT_MEMORY_TYPE_WB  6

// EPT access bits
#define EPT_READ    (1ULL << 0)
#define EPT_WRITE   (1ULL << 1)
#define EPT_EXECUTE (1ULL << 2)
#define EPT_RWX     (EPT_READ | EPT_WRITE | EPT_EXECUTE)

typedef struct _VMXON_REGION {
    UINT32 RevisionId;
    UINT8  Data[4092];
} VMXON_REGION, *PVMXON_REGION;

typedef struct _VMCS_REGION {
    UINT32 RevisionId;
    UINT32 AbortIndicator;
    UINT8  Data[4088];
} VMCS_REGION, *PVMCS_REGION;

#pragma pack(push, 1)
typedef struct _SEGMENT_DESCRIPTOR {
    UINT16 LimitLow;
    UINT16 BaseLow;
    UINT8  BaseMid;
    UINT8  Attributes1;
    UINT8  LimitHigh_Attributes2;
    UINT8  BaseHigh;
} SEGMENT_DESCRIPTOR, *PSEGMENT_DESCRIPTOR;

typedef struct _GDTR {
    UINT16 Limit;
    UINT64 Base;
} GDTR, *PGDTR;
#pragma pack(pop)

typedef struct _GUEST_REGS {
    UINT64 Rax;
    UINT64 Rcx;
    UINT64 Rdx;
    UINT64 Rbx;
    UINT64 Rsp;
    UINT64 Rbp;
    UINT64 Rsi;
    UINT64 Rdi;
    UINT64 R8;
    UINT64 R9;
    UINT64 R10;
    UINT64 R11;
    UINT64 R12;
    UINT64 R13;
    UINT64 R14;
    UINT64 R15;
} GUEST_REGS, *PGUEST_REGS;
