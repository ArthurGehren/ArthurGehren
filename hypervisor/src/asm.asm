;
; asm.asm - x64 MASM assembly stubs for VMX operations
;
; AsmVmxLaunch     - Save guest state, write RIP/RSP to VMCS, VMLAUNCH
; AsmVmxEntryPoint - VM exit entry point (host RIP in VMCS)
; AsmVmxVmCall     - Issue VMCALL instruction
;

EXTERN VmxExitHandler:PROC

; VMCS field encodings we use in assembly
VMCS_GUEST_RSP  EQU 0681Ch
VMCS_GUEST_RIP  EQU 0681Eh

.CODE

;----------------------------------------------------------------------
; AsmVmxLaunch
;   - Captures current RSP/RIP into VMCS guest state
;   - Executes VMLAUNCH
;   - On success, execution continues at the label AFTER vmlaunch
;     (the guest is now running under the hypervisor)
;   - On failure, returns to caller with error
;----------------------------------------------------------------------
AsmVmxLaunch PROC

    ; Save all nonvolatile registers (these form the "guest state")
    pushfq
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    ; Write current RSP as guest RSP
    mov     rcx, VMCS_GUEST_RSP
    mov     rdx, rsp
    vmwrite rcx, rdx
    jc      LaunchFailed
    jz      LaunchFailed

    ; Write the "resume point" as guest RIP
    ; When VMLAUNCH succeeds, the guest starts executing at GuestResumePoint
    mov     rcx, VMCS_GUEST_RIP
    lea     rdx, GuestResumePoint
    vmwrite rcx, rdx
    jc      LaunchFailed
    jz      LaunchFailed

    ; VMLAUNCH
    vmlaunch

    ; If we reach here, VMLAUNCH failed
LaunchFailed:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax
    popfq
    ret

    ; === Guest resumes here after successful VMLAUNCH ===
GuestResumePoint:
    ; Restore all registers we pushed
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax
    popfq
    ret

AsmVmxLaunch ENDP

;----------------------------------------------------------------------
; AsmVmxEntryPoint
;   - This is where the CPU jumps on every VM exit (HOST_RIP)
;   - Save guest general-purpose registers
;   - Call VmxExitHandler(CpuState)
;   - If handler returns TRUE, do VMRESUME
;   - If FALSE, devirtualize (restore guest state and return)
;----------------------------------------------------------------------
AsmVmxEntryPoint PROC

    ; Save all guest GPRs on the host stack
    push    r15
    push    r14
    push    r13
    push    r12
    push    r11
    push    r10
    push    r9
    push    r8
    push    rdi
    push    rsi
    push    rbp
    push    rbx
    push    rdx
    push    rcx
    push    rax

    ; First param (RCX) = pointer to VMX_CPU_STATE
    ; We stored a pointer to it at the base of the host stack
    ; For simplicity, pass RSP as context (the saved register frame)
    mov     rcx, rsp

    ; Align stack to 16 bytes for the call
    sub     rsp, 28h
    call    VmxExitHandler
    add     rsp, 28h

    ; Check return value
    test    al, al
    jz      Devirtualize

    ; Resume guest
    pop     rax
    pop     rcx
    pop     rdx
    pop     rbx
    pop     rbp
    pop     rsi
    pop     rdi
    pop     r8
    pop     r9
    pop     r10
    pop     r11
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    vmresume

    ; VMRESUME failed — halt (should not happen)
    int     3
    jmp     $

Devirtualize:
    ; Restore guest state and leave VMX operation
    ; Read guest RSP and RIP from VMCS
    mov     rcx, VMCS_GUEST_RSP
    vmread  rdx, rcx            ; rdx = guest RSP

    mov     rcx, VMCS_GUEST_RIP
    vmread  rbx, rcx            ; rbx = guest RIP

    ; Pop saved guest registers
    pop     rax
    pop     rcx
    pop     rdx
    add     rsp, 8              ; skip rbx (we need it for RIP)
    pop     rbp
    pop     rsi
    pop     rdi
    pop     r8
    pop     r9
    pop     r10
    pop     r11
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    ; VMXOFF
    vmxoff

    ; Switch to guest stack and jump to guest RIP
    mov     rsp, rdx            ; guest RSP
    jmp     rbx                 ; guest RIP

AsmVmxEntryPoint ENDP

;----------------------------------------------------------------------
; AsmVmxVmCall(HypercallId, Param1, Param2)
;   RCX = HypercallId, RDX = Param1, R8 = Param2
;   Returns RAX
;----------------------------------------------------------------------
AsmVmxVmCall PROC
    vmcall
    ret
AsmVmxVmCall ENDP

END
