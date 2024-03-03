.thumb
.syntax unified

//---- INTERNAL FUNCTIONS ----------------------------------------------------------------------------------------------------------------------------------------

// Supervisor Call interrupt handler. Triggered by the kernel_yield() function
// saves the process state to the Process Stack and loads kernel state from the Main Stack
// after this, the kernel continues execution and finds next task to switch to
.type SVCall_Handler, %function
.thumb_func
.global SVCall_Handler
SVCall_Handler:

    // hardware automatically pushes PSR, PC, LR, R12, R3, R2, R1, R0 to the Process Stack in this order

    // ---- SAVE PROCESS STATE ----
    // push LR to Process Stack (exception call automatically switches to Main Stack, so we have to push to Process Stack manually)
    mrs  r0, psp
    subs r0, #4         // R0 = Process Stack Pointer - 4
    mov  r1, lr
    str  r1, [r0]       // store contents of LR to PSP - 4

    // push R7, R6, R5, R4 in this order to Process Stack
    subs  r0 , #16
    stmia r0!, {r4, r5, r6, r7}

    // push R11, R10, R9, R8 in this order to Process Stack
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    subs  r0 , #32
    stmia r0!, {r4, r5, r6, r7}
    subs  r0 , #16      // R0 now holds the new PSP

    // ---- LOAD KERNEL STATE FROM STACK ----
    // pop R8, R9, R10, R11, R12, R4, R5, R6, R7 from Main Stack in this order
    pop {r1, r2, r3, r4, r5}
    mov r8,  r1
    mov r9,  r2
    mov r10, r3
    mov r11, r4
    mov r12, r5
    pop {r4, r5, r6, r7}

    msr psr_nzcvq, ip   // restore kernel ALU flags stored in IP (R12)
    pop {pc}            // pop LR from stack to Program Counter (jump to kernel code)

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// switches to specified task. Takes task's PSP as an argument
// called by the kernel to save the kernel state to the Main Stack and load the process state
.type __kernel_switch_to_task, %function
.thumb_func
.global __kernel_switch_to_task
__kernel_switch_to_task:

    // ---- SAVE KERNEL STATE ----
    // push LR, R7, R6, R5, R4, PSR, R11, R10, R9, R8 to Main Stack in this order
    mrs  ip, psr
    push {r4, r5, r6, r7, lr}
    mov  r1, r8
    mov  r2, r9
    mov  r3, r10
    mov  r4, r11
    mov  r5, r12                // R12 is IP
    push {r1, r2, r3, r4, r5}

    // ---- LOAD PROCESS STATE ----
    // pop R8, R9, R10, R11, R4, R5, R6, R7, LR from Process Stack in this order (we are running this subroutine in kernel and using Main Stack, so we have to push to Process Stack manually)
    ldmia r0!, {r4, r5, r6, r7}
    mov   r8 , r4
    mov   r9 , r5
    mov   r10, r6
    mov   r11, r7
    ldmia r0!, {r4, r5, r6, r7}
    ldmia r0!, {r1}
    mov   lr, r1    // LR contains 0xffff fffd, which causes exception return and the processor will automatically pop R0, R1, R2, R3, LR, PC and PSR from the exception frame

    msr psp, r0     // set Process Stack Pointer to specified task stack
    bx lr           // jump to task. branch to 0xffff fffd causes exception return and automatic return to the task

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// sets the current stack to the Process Stack, then triggers the Supervisor Call interrupt which pushes the process state onto the Process Stack. After returning, the kernel now runs in the Main Stack.
.type __kernel_init_stack, %function
.thumb_func
.global __kernel_init_stack
__kernel_init_stack:

    mrs ip, psr     // move Program Status Register to IP (R12)

    // push LR (return address), R7, R6, R5, R4, R12, R11, R10, R9, R8 in this order to Main Stack 
    push {r4, r5, r6, r7, lr}
    mov  r1, r8
    mov  r2, r9
    mov  r3, r10
    mov  r4, r11
    mov  r5, r12
    push {r1, r2, r3, r4, r5}

    msr psp, r0     // move R0 (function argument) to Process Stack Pointer
    movs r0, #2     
    msr control, r0 // switch to Process Stack and priviledged thread mode (bit 0 of CONTROL is nPRIV, bit 1 is SPSEL)
    isb             // flush instruction pipeline before proceeding

    svc 0           // trigger Supervisor Call interrupt
    bx lr           // return from subroutine

//----------------------------------------------------------------------------------------------------------------------------------------------------------------
