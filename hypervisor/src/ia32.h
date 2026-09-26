#pragma once
#include <ntddk.h>

// ── MSRs ──
#define MSR_IA32_FEATURE_CONTROL        0x3A
#define MSR_IA32_VMX_BASIC              0x480
#define MSR_IA32_VMX_PINBASED_CTLS      0x481
#define MSR_IA32_VMX_PROCBASED_CTLS     0x482
#define MSR_IA32_VMX_EXIT_CTLS          0x483
#define MSR_IA32_VMX_ENTRY_CTLS         0x484
#define MSR_IA32_VMX_CR0_FIXED0         0x486
#define MSR_IA32_VMX_CR0_FIXED1         0x487
#define MSR_IA32_VMX_CR4_FIXED0         0x488
#define MSR_IA32_VMX_CR4_FIXED1         0x489
#define MSR_IA32_VMX_PROCBASED_CTLS2    0x48B
#define MSR_IA32_VMX_EPT_VPID_CAP      0x48C
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS 0x48D
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS 0x48E
#define MSR_IA32_VMX_TRUE_EXIT_CTLS     0x48F
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS    0x490
#define MSR_IA32_DEBUGCTL               0x1D9
#define MSR_IA32_SYSENTER_CS            0x174
#define MSR_IA32_SYSENTER_ESP           0x175
#define MSR_IA32_SYSENTER_EIP           0x176
#define MSR_FS_BASE                     0xC0000100
#define MSR_GS_BASE                     0xC0000101
#define MSR_SHADOW_GS_BASE              0xC0000102
#define MSR_IA32_PAT                    0x277
#define MSR_IA32_EFER                   0xC0000080

// ── Feature control bits ──
#define FEATURE_CONTROL_LOCKED          (1ULL << 0)
#define FEATURE_CONTROL_VMXON_OUTSIDE   (1ULL << 2)

// ── CR4 ──
#define CR4_VMXE                        (1ULL << 13)

// ── RFLAGS ──
#define RFLAGS_CF                       (1ULL << 0)
#define RFLAGS_ZF                       (1ULL << 6)

// ── VMCS field encodings (Intel SDM Vol 3, Appendix B) ──

// 16-bit control
#define VMCS_VPID                       0x0000

// 16-bit guest
#define VMCS_GUEST_ES_SEL               0x0800
#define VMCS_GUEST_CS_SEL               0x0802
#define VMCS_GUEST_SS_SEL               0x0804
#define VMCS_GUEST_DS_SEL               0x0806
#define VMCS_GUEST_FS_SEL               0x0808
#define VMCS_GUEST_GS_SEL               0x080A
#define VMCS_GUEST_LDTR_SEL             0x080C
#define VMCS_GUEST_TR_SEL               0x080E

// 16-bit host
#define VMCS_HOST_ES_SEL                0x0C00
#define VMCS_HOST_CS_SEL                0x0C02
#define VMCS_HOST_SS_SEL                0x0C04
#define VMCS_HOST_DS_SEL                0x0C06
#define VMCS_HOST_FS_SEL                0x0C08
#define VMCS_HOST_GS_SEL                0x0C0A
#define VMCS_HOST_TR_SEL                0x0C0E

// 64-bit control
#define VMCS_IO_BITMAP_A                0x2000
#define VMCS_IO_BITMAP_B                0x2002
#define VMCS_MSR_BITMAP                 0x2004
#define VMCS_EPT_POINTER                0x201A
#define VMCS_TSC_OFFSET                 0x2010

// 64-bit guest
#define VMCS_GUEST_VMCS_LINK_PTR        0x2800
#define VMCS_GUEST_DEBUGCTL             0x2802
#define VMCS_GUEST_PAT                  0x2804
#define VMCS_GUEST_EFER                 0x2806

// 64-bit host
#define VMCS_HOST_PAT                   0x2C00
#define VMCS_HOST_EFER                  0x2C02

// 32-bit control
#define VMCS_PIN_BASED_CONTROLS         0x4000
#define VMCS_PROC_BASED_CONTROLS        0x4002
#define VMCS_EXCEPTION_BITMAP           0x4004
#define VMCS_PF_ERROR_CODE_MASK         0x4006
#define VMCS_PF_ERROR_CODE_MATCH        0x4008
#define VMCS_CR3_TARGET_COUNT           0x400A
#define VMCS_EXIT_CONTROLS              0x400C
#define VMCS_EXIT_MSR_STORE_COUNT       0x400E
#define VMCS_EXIT_MSR_LOAD_COUNT        0x4010
#define VMCS_ENTRY_CONTROLS             0x4012
#define VMCS_ENTRY_MSR_LOAD_COUNT       0x4014
#define VMCS_ENTRY_INTERRUPTION_INFO    0x4016
#define VMCS_ENTRY_EXCEPTION_ERROR      0x4018
#define VMCS_ENTRY_INSTRUCTION_LEN      0x401A
#define VMCS_PROC_BASED_CONTROLS2       0x401E

// 32-bit read-only
#define VMCS_VM_INSTRUCTION_ERROR       0x4400
#define VMCS_EXIT_REASON                0x4402
#define VMCS_EXIT_INTERRUPTION_INFO     0x4404
#define VMCS_EXIT_INTERRUPTION_ERROR    0x4406
#define VMCS_IDT_VECTORING_INFO         0x4408
#define VMCS_IDT_VECTORING_ERROR        0x440A
#define VMCS_EXIT_INSTRUCTION_LEN       0x440C
#define VMCS_EXIT_INSTRUCTION_INFO      0x440E

// 32-bit guest
#define VMCS_GUEST_ES_LIMIT             0x4800
#define VMCS_GUEST_CS_LIMIT             0x4802
#define VMCS_GUEST_SS_LIMIT             0x4804
#define VMCS_GUEST_DS_LIMIT             0x4806
#define VMCS_GUEST_FS_LIMIT             0x4808
#define VMCS_GUEST_GS_LIMIT             0x480A
#define VMCS_GUEST_LDTR_LIMIT           0x480C
#define VMCS_GUEST_TR_LIMIT             0x480E
#define VMCS_GUEST_GDTR_LIMIT           0x4810
#define VMCS_GUEST_IDTR_LIMIT           0x4812
#define VMCS_GUEST_ES_ACCESS            0x4814
#define VMCS_GUEST_CS_ACCESS            0x4816
#define VMCS_GUEST_SS_ACCESS            0x4818
#define VMCS_GUEST_DS_ACCESS            0x481A
#define VMCS_GUEST_FS_ACCESS            0x481C
#define VMCS_GUEST_GS_ACCESS            0x481E
#define VMCS_GUEST_LDTR_ACCESS          0x4820
#define VMCS_GUEST_TR_ACCESS            0x4822
#define VMCS_GUEST_INTERRUPTIBILITY     0x4824
#define VMCS_GUEST_ACTIVITY             0x4826
#define VMCS_GUEST_SYSENTER_CS          0x482A

// 32-bit host
#define VMCS_HOST_SYSENTER_CS           0x4C00

// Natural-width control
#define VMCS_CR0_GUEST_HOST_MASK        0x6000
#define VMCS_CR4_GUEST_HOST_MASK        0x6002
#define VMCS_CR0_READ_SHADOW            0x6004
#define VMCS_CR4_READ_SHADOW            0x6006

// Natural-width read-only
#define VMCS_EXIT_QUALIFICATION         0x6400
#define VMCS_GUEST_LINEAR_ADDRESS       0x640A
#define VMCS_GUEST_PHYSICAL_ADDRESS     0x2400

// Natural-width guest
#define VMCS_GUEST_CR0                  0x6800
#define VMCS_GUEST_CR3                  0x6802
#define VMCS_GUEST_CR4                  0x6804
#define VMCS_GUEST_ES_BASE              0x6806
#define VMCS_GUEST_CS_BASE              0x6808
#define VMCS_GUEST_SS_BASE              0x680A
#define VMCS_GUEST_DS_BASE              0x680C
#define VMCS_GUEST_FS_BASE              0x680E
#define VMCS_GUEST_GS_BASE              0x6810
#define VMCS_GUEST_LDTR_BASE            0x6812
#define VMCS_GUEST_TR_BASE              0x6814
#define VMCS_GUEST_GDTR_BASE            0x6816
#define VMCS_GUEST_IDTR_BASE            0x6818
#define VMCS_GUEST_DR7                  0x681A
#define VMCS_GUEST_RSP                  0x681C
#define VMCS_GUEST_RIP                  0x681E
#define VMCS_GUEST_RFLAGS               0x6820
#define VMCS_GUEST_SYSENTER_ESP         0x6824
#define VMCS_GUEST_SYSENTER_EIP         0x6826

// Natural-width host
#define VMCS_HOST_CR0                   0x6C00
#define VMCS_HOST_CR3                   0x6C02
#define VMCS_HOST_CR4                   0x6C04
#define VMCS_HOST_FS_BASE               0x6C06
#define VMCS_HOST_GS_BASE               0x6C08
#define VMCS_HOST_TR_BASE               0x6C0A
#define VMCS_HOST_GDTR_BASE             0x6C0C
#define VMCS_HOST_IDTR_BASE             0x6C0E
#define VMCS_HOST_SYSENTER_ESP          0x6C10
#define VMCS_HOST_SYSENTER_EIP          0x6C12
#define VMCS_HOST_RSP                   0x6C14
#define VMCS_HOST_RIP                   0x6C16

// ── VM exit reasons ──
#define EXIT_REASON_EXCEPTION_NMI       0
#define EXIT_REASON_EXTERNAL_INT        1
#define EXIT_REASON_TRIPLE_FAULT        2
#define EXIT_REASON_CPUID               10
#define EXIT_REASON_INVD                13
#define EXIT_REASON_VMCALL              18
#define EXIT_REASON_CR_ACCESS           28
#define EXIT_REASON_MSR_READ            31
#define EXIT_REASON_MSR_WRITE           32
#define EXIT_REASON_EPT_VIOLATION       48
#define EXIT_REASON_EPT_MISCONFIG       49
#define EXIT_REASON_XSETBV              55

// ── Primary proc-based controls ──
#define PROC_BASED_HLT_EXITING          (1UL << 7)
#define PROC_BASED_CR3_LOAD_EXITING     (1UL << 15)
#define PROC_BASED_CR3_STORE_EXITING    (1UL << 16)
#define PROC_BASED_USE_MSR_BITMAPS      (1UL << 28)
#define PROC_BASED_ACTIVATE_SECONDARY   (1UL << 31)

// ── Secondary proc-based controls ──
#define PROC_BASED2_ENABLE_EPT          (1UL << 1)
#define PROC_BASED2_UNRESTRICTED_GUEST  (1UL << 7)
#define PROC_BASED2_ENABLE_VPID         (1UL << 5)
#define PROC_BASED2_ENABLE_INVPCID      (1UL << 12)
#define PROC_BASED2_ENABLE_XSAVES      (1UL << 20)

// ── Pin-based controls ──
#define PIN_BASED_NMI_EXITING           (1UL << 3)

// ── Exit controls ──
#define EXIT_CONTROL_HOST_ADDR_SPACE    (1UL << 9)
#define EXIT_CONTROL_SAVE_PAT           (1UL << 18)
#define EXIT_CONTROL_LOAD_PAT           (1UL << 19)
#define EXIT_CONTROL_SAVE_EFER          (1UL << 20)
#define EXIT_CONTROL_LOAD_EFER          (1UL << 21)

// ── Entry controls ──
#define ENTRY_CONTROL_IA32E_MODE        (1UL << 9)
#define ENTRY_CONTROL_LOAD_PAT          (1UL << 14)
#define ENTRY_CONTROL_LOAD_EFER         (1UL << 15)

// ── EPT violation qualifications ──
#define EPT_VIOLATION_READ              (1ULL << 0)
#define EPT_VIOLATION_WRITE             (1ULL << 1)
#define EPT_VIOLATION_EXECUTE           (1ULL << 2)

// ── Segment descriptor ──
#pragma pack(push, 1)
typedef struct _SEGMENT_DESCRIPTOR {
    USHORT LimitLow;
    USHORT BaseLow;
    UCHAR  BaseMid;
    UCHAR  Flags1;
    UCHAR  Flags2;
    UCHAR  BaseHigh;
} SEGMENT_DESCRIPTOR, *PSEGMENT_DESCRIPTOR;

typedef struct _SEGMENT_DESCRIPTOR_64 {
    USHORT LimitLow;
    USHORT BaseLow;
    UCHAR  BaseMid;
    UCHAR  Flags1;
    UCHAR  Flags2;
    UCHAR  BaseHigh;
    ULONG  BaseUpper;
    ULONG  Reserved;
} SEGMENT_DESCRIPTOR_64, *PSEGMENT_DESCRIPTOR_64;
#pragma pack(pop)

typedef struct _DESCRIPTOR_TABLE_REG {
    USHORT Limit;
    ULONG64 Base;
} DESCRIPTOR_TABLE_REG, *PDESCRIPTOR_TABLE_REG;

// ── CPUID result ──
typedef struct _CPUID_RESULT {
    ULONG Eax;
    ULONG Ebx;
    ULONG Ecx;
    ULONG Edx;
} CPUID_RESULT;
