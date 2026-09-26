; ============================================================
; vmx_asm.asm — VMX launch/resume entry point and VM-exit stub
; Assembled with MASM (ml64)
; ============================================================

EXTERN VmExitHandler : PROC

.CODE

; void AsmVmxLaunch(PGUEST_REGS GuestRegs);
; RCX = pointer to GUEST_REGS on host stack
AsmVmxLaunch PROC
    ; Save host non-volatile registers
    push    rbp
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    ; Save GuestRegs pointer
    mov     rbp, rcx

    ; Restore guest general-purpose registers from GUEST_REGS
    mov     rax, [rbp + 000h]   ; Rax
    mov     rcx, [rbp + 008h]   ; Rcx
    mov     rdx, [rbp + 010h]   ; Rdx
    mov     rbx, [rbp + 018h]   ; Rbx
    ; skip RSP (020h) — managed by VMCS
    ; skip RBP (028h) — we need it
    mov     rsi, [rbp + 030h]   ; Rsi
    mov     rdi, [rbp + 038h]   ; Rdi
    mov     r8,  [rbp + 040h]   ; R8
    mov     r9,  [rbp + 048h]   ; R9
    mov     r10, [rbp + 050h]   ; R10
    mov     r11, [rbp + 058h]   ; R11
    mov     r12, [rbp + 060h]   ; R12
    mov     r13, [rbp + 068h]   ; R13
    mov     r14, [rbp + 070h]   ; R14
    mov     r15, [rbp + 078h]   ; R15

    ; Load RBP last
    push    rax
    mov     rax, [rbp + 028h]
    xchg    rbp, rax
    pop     rax

    vmlaunch

    ; If vmlaunch fails, we end up here
    jmp     VmxLaunchFailed

AsmVmxLaunch ENDP

; VM-exit entry point — set as HOST_RIP in VMCS
AsmVmExitHandler PROC
    ; Push all guest registers (they were live at exit)
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
    sub     rsp, 8      ; placeholder for RSP in GUEST_REGS
    push    rbx
    push    rdx
    push    rcx
    push    rax

    ; RCX = pointer to GUEST_REGS (on stack)
    mov     rcx, rsp
    sub     rsp, 28h    ; shadow space
    call    VmExitHandler
    add     rsp, 28h

    ; AL = return from VmExitHandler: TRUE=resume, FALSE=stop
    test    al, al
    jz      VmxStop

    ; Restore guest GPRs and VMRESUME
    pop     rax
    pop     rcx
    pop     rdx
    pop     rbx
    add     rsp, 8      ; skip RSP placeholder
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

    ; vmresume failed
    jmp     VmxLaunchFailed

VmxStop:
    ; Devirtualize — restore guest state and return to guest
    pop     rax
    pop     rcx
    pop     rdx
    pop     rbx
    pop     rsp         ; restore guest RSP
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

    ; Read guest RIP from VMCS (already advanced past VMCALL)
    ; We stored it in RAX via the exit handler
    vmxoff
    ; Jump to guest RIP (stored in RAX by convention)
    ret

AsmVmExitHandler ENDP

VmxLaunchFailed PROC
    ; Restore host non-volatile registers
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    pop     rbp
    ret
VmxLaunchFailed ENDP

END
